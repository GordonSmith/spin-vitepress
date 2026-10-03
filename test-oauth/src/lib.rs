//! GitHub OAuth 2.0 login component.
//!
//! Routes (relative to the component route, e.g. `/auth`):
//! - `/`          – shows the signed-in user, or a sign-in page when signed out
//! - `/login`     – starts the OAuth flow by redirecting to GitHub
//!                  (optional `?return_to=/path` to come back to after sign-in)
//! - `/callback`  – OAuth redirect target; exchanges the code for a token
//! - `/user`      – JSON for the signed-in user (401 when signed out)
//! - `/logout`    – clears the session cookie and returns to the sign-in page
//!                  (or to `?return_to=/path` when given)

use std::time::{SystemTime, UNIX_EPOCH};

use anyhow::{anyhow, bail, Context, Result};
use base64::engine::general_purpose::URL_SAFE_NO_PAD as B64;
use base64::Engine;
use hmac::{Hmac, KeyInit, Mac};
use serde::{Deserialize, Serialize};
use sha2::Sha256;
use spin_sdk::http::conversions::TryFromIncomingRequest;
use spin_sdk::http::{send, Fields, IncomingRequest, OutgoingResponse, Request, Response, ResponseOutparam};
use spin_sdk::{http_component, variables};

const AUTHORIZE_URL: &str = "https://github.com/login/oauth/authorize";
const TOKEN_URL: &str = "https://github.com/login/oauth/access_token";
const USER_URL: &str = "https://api.github.com/user";
const USER_AGENT: &str = "spin-vitepress-oauth";

const STATE_COOKIE: &str = "oauth_state";
const RETURN_COOKIE: &str = "oauth_return";
const SESSION_COOKIE: &str = "oauth_session";
const STATE_TTL_SECS: u64 = 600;
const SESSION_TTL_SECS: u64 = 60 * 60 * 8;

struct Config {
    client_id: String,
    client_secret: String,
    scope: String,
    redirect_uri: Option<String>,
}

impl Config {
    fn load() -> Option<Self> {
        let get = |name: &str| variables::get(name).ok().filter(|v| !v.trim().is_empty());
        Some(Self {
            client_id: get("client_id")?,
            client_secret: get("client_secret")?,
            scope: get("scope").unwrap_or_else(|| "read:user".into()),
            redirect_uri: get("redirect_uri"),
        })
    }
}

#[derive(Serialize, Deserialize)]
struct Session {
    login: String,
    name: Option<String>,
    avatar_url: Option<String>,
    html_url: Option<String>,
    exp: u64,
}

#[derive(Deserialize)]
struct GitHubUser {
    login: String,
    name: Option<String>,
    avatar_url: Option<String>,
    html_url: Option<String>,
}

#[derive(Deserialize)]
struct TokenResponse {
    access_token: Option<String>,
    error: Option<String>,
    error_description: Option<String>,
}

/// Request-derived context (where we are mounted and how we were reached).
struct Ctx {
    base_url: String,
    base_path: String,
    secure: bool,
}

impl Ctx {
    fn from_request(req: &Request) -> Self {
        let header = |name: &str| req.header(name).and_then(|v| v.as_str()).unwrap_or("");
        let full_url = header("spin-full-url");
        let (scheme, rest) = full_url.split_once("://").unwrap_or(("http", full_url));
        let authority = rest.split('/').next().unwrap_or("");
        let scheme = match header("x-forwarded-proto") {
            "" => scheme,
            proto => proto.split(',').next().unwrap_or(proto).trim(),
        };
        let full_path = rest[authority.len()..].split(['?', '#']).next().unwrap_or("");
        let base_path = base_path(full_path, header("spin-path-info"), header("spin-component-route"));
        Self {
            base_url: format!("{scheme}://{authority}{base_path}"),
            base_path,
            secure: scheme == "https",
        }
    }

    /// Absolute URL for a route below the mount point, e.g. `/logout`.
    fn url(&self, suffix: &str) -> String {
        format!("{}{suffix}", self.base_url)
    }

    fn cookie(&self, name: &str, value: &str, max_age: u64) -> String {
        let path = if self.base_path.is_empty() { "/" } else { &self.base_path };
        let secure = if self.secure { "; Secure" } else { "" };
        // Pair Max-Age with Expires so deletions are honoured by old/strict clients.
        let expires = if max_age == 0 {
            "; Expires=Thu, 01 Jan 1970 00:00:00 GMT"
        } else {
            ""
        };
        format!("{name}={value}; Path={path}; Max-Age={max_age}{expires}; HttpOnly; SameSite=Lax{secure}")
    }
}

/// Where this component is mounted, derived from the request path rather than
/// trusting a single header: runtimes that omit `spin-component-route` would
/// otherwise produce root-relative links and cookies that miss the mount point.
fn base_path(full_path: &str, path_info: &str, component_route: &str) -> String {
    let strip = |s: &str| s.trim_end_matches('/').to_string();
    if !full_path.is_empty() && full_path.ends_with(path_info) {
        return strip(&full_path[..full_path.len() - path_info.len()]);
    }
    strip(component_route.trim_end_matches("..."))
}

/// A buffered response. Unlike the SDK's `Response`, header names may repeat
/// (needed to send more than one `Set-Cookie`).
struct Reply {
    status: u16,
    headers: Vec<(String, String)>,
    body: Vec<u8>,
}

impl Reply {
    fn new(status: u16, content_type: &str, body: impl Into<Vec<u8>>) -> Self {
        Self {
            status,
            headers: vec![
                ("content-type".into(), content_type.into()),
                ("cache-control".into(), "no-store".into()),
            ],
            body: body.into(),
        }
    }

    fn with_cookie(mut self, cookie: String) -> Self {
        self.headers.push(("set-cookie".into(), cookie));
        self
    }

    async fn send(self, out: ResponseOutparam) {
        let headers: Vec<(String, Vec<u8>)> = self
            .headers
            .into_iter()
            .map(|(k, v)| (k, v.into_bytes()))
            .collect();
        let fields = Fields::from_list(&headers).expect("valid response headers");
        let response = OutgoingResponse::new(fields);
        let _ = response.set_status_code(self.status);
        if let Err(e) = out.set_with_body(response, self.body).await {
            eprintln!("oauth: failed to write response body: {e:?}");
        }
    }
}

#[http_component]
async fn handle(incoming: IncomingRequest, out: ResponseOutparam) {
    let reply = match Request::try_from_incoming_request(incoming).await {
        Ok(req) => route(&req).await,
        Err(_) => Reply::new(400, "text/plain", "Bad request"),
    };
    reply.send(out).await;
}

async fn route(req: &Request) -> Reply {
    let ctx = Ctx::from_request(req);
    let Some(config) = Config::load() else {
        return html(503, NOT_CONFIGURED.to_string());
    };
    let path = req
        .header("spin-path-info")
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .trim_end_matches('/')
        .to_string();

    let result = match path.as_str() {
        "" => Ok(index(req, &ctx, &config)),
        "/login" => authorize(req, &ctx, &config),
        "/callback" => callback(req, &ctx, &config).await,
        "/user" => Ok(user(req, &config)),
        "/logout" => Ok(logout(req, &ctx)),
        _ => Ok(Reply::new(404, "text/plain", "Not found")),
    };

    result.unwrap_or_else(|e| {
        eprintln!("oauth error: {e:#}");
        html(
            400,
            format!(
                "<h1>Login failed</h1><p>{}</p><p><a href=\"{}\">Try again</a></p>",
                escape(&format!("{e:#}")),
                escape(&ctx.base_url)
            ),
        )
        .with_cookie(ctx.cookie(STATE_COOKIE, "", 0))
        .with_cookie(ctx.cookie(RETURN_COOKIE, "", 0))
    })
}

fn index(req: &Request, ctx: &Ctx, config: &Config) -> Reply {
    if let Some(session) = current_session(req, config) {
        let name = session.name.as_deref().unwrap_or(&session.login);
        let avatar = session
            .avatar_url
            .as_deref()
            .map(|url| format!("<img src=\"{}\" width=\"96\" height=\"96\" alt=\"\">", escape(url)))
            .unwrap_or_default();
        let body = format!(
            "{avatar}<h1>Hello, {}!</h1><p>Signed in with GitHub as <a href=\"{}\">@{}</a>.</p>\
             <p><a href=\"/\">Home</a> · <a href=\"{}\">Sign out</a></p>",
            escape(name),
            escape(session.html_url.as_deref().unwrap_or("#")),
            escape(&session.login),
            escape(&ctx.url("/logout")),
        );
        return html(200, body);
    }

    let signed_out = form_urlencoded::parse(req.query().as_bytes())
        .any(|(k, v)| k == "signed_out" && v == "1");
    let note = if signed_out {
        "<p>You are now signed out.</p>"
    } else {
        ""
    };
    html(
        200,
        format!(
            "<h1>Sign in</h1>{note}<p><a href=\"{}\">Sign in with GitHub</a></p>\
             <p><a href=\"/\">Home</a></p>",
            escape(&ctx.url("/login")),
        ),
    )
}

fn authorize(req: &Request, ctx: &Ctx, config: &Config) -> Result<Reply> {
    let state = random_token()?;
    let redirect_uri = redirect_uri(ctx, config);
    let query = form_urlencoded::Serializer::new(String::new())
        .append_pair("client_id", &config.client_id)
        .append_pair("redirect_uri", &redirect_uri)
        .append_pair("scope", &config.scope)
        .append_pair("state", &state)
        .append_pair("allow_signup", "true")
        .finish();
    let return_cookie = match return_to(req) {
        Some(path) => ctx.cookie(RETURN_COOKIE, &B64.encode(path), STATE_TTL_SECS),
        None => ctx.cookie(RETURN_COOKIE, "", 0),
    };
    Ok(redirect(&format!("{AUTHORIZE_URL}?{query}"))
        .with_cookie(ctx.cookie(STATE_COOKIE, &state, STATE_TTL_SECS))
        .with_cookie(return_cookie))
}

/// The `return_to` query parameter, if it is a safe same-origin path.
fn return_to(req: &Request) -> Option<String> {
    form_urlencoded::parse(req.query().as_bytes())
        .find(|(k, _)| k == "return_to")
        .map(|(_, v)| v.into_owned())
        .filter(|v| is_local_path(v))
}

/// Accepts only absolute paths on this origin, rejecting protocol-relative
/// (`//host`) and backslash tricks that browsers treat as another origin.
fn is_local_path(path: &str) -> bool {
    path.starts_with('/')
        && !path.starts_with("//")
        && !path.contains('\\')
        && !path.chars().any(|c| c.is_control())
}

/// Clears the session (and any in-flight state) and returns to the sign-in page,
/// so the user gets explicit confirmation instead of a silent re-authorization.
/// With a `return_to` path, the user is sent back there instead.
fn logout(req: &Request, ctx: &Ctx) -> Reply {
    let location = return_to(req).unwrap_or_else(|| ctx.url("/?signed_out=1"));
    redirect(&location)
        .with_cookie(ctx.cookie(SESSION_COOKIE, "", 0))
        .with_cookie(ctx.cookie(STATE_COOKIE, "", 0))
        .with_cookie(ctx.cookie(RETURN_COOKIE, "", 0))
}

async fn callback(req: &Request, ctx: &Ctx, config: &Config) -> Result<Reply> {
    let params: Vec<(String, String)> = form_urlencoded::parse(req.query().as_bytes())
        .into_owned()
        .collect();
    let param = |name: &str| params.iter().find(|(k, _)| k == name).map(|(_, v)| v.as_str());

    if let Some(error) = param("error") {
        bail!("{}", param("error_description").unwrap_or(error));
    }
    let code = param("code").context("missing code parameter")?;
    let state = param("state").context("missing state parameter")?;
    let expected = cookie(req, STATE_COOKIE).context("missing state cookie (expired?)")?;
    if !constant_time_eq(state.as_bytes(), expected.as_bytes()) {
        bail!("state mismatch");
    }

    let token = exchange_code(code, ctx, config).await?;
    let user = fetch_user(&token).await?;

    let session = Session {
        login: user.login,
        name: user.name,
        avatar_url: user.avatar_url,
        html_url: user.html_url,
        exp: now() + SESSION_TTL_SECS,
    };
    let value = sign(&serde_json::to_vec(&session)?, config)?;
    let location = cookie(req, RETURN_COOKIE)
        .and_then(|v| B64.decode(v).ok())
        .and_then(|v| String::from_utf8(v).ok())
        .filter(|v| is_local_path(v))
        .unwrap_or_else(|| ctx.base_url.clone());
    Ok(redirect(&location)
        .with_cookie(ctx.cookie(STATE_COOKIE, "", 0))
        .with_cookie(ctx.cookie(RETURN_COOKIE, "", 0))
        .with_cookie(ctx.cookie(SESSION_COOKIE, &value, SESSION_TTL_SECS)))
}

fn user(req: &Request, config: &Config) -> Reply {
    let Some(session) = current_session(req, config) else {
        return Reply::new(401, "application/json", r#"{"error":"unauthenticated"}"#);
    };
    let body = serde_json::json!({
        "login": session.login,
        "name": session.name,
        "avatar_url": session.avatar_url,
        "html_url": session.html_url,
    });
    Reply::new(200, "application/json", body.to_string())
}

async fn exchange_code(code: &str, ctx: &Ctx, config: &Config) -> Result<String> {
    let body = form_urlencoded::Serializer::new(String::new())
        .append_pair("client_id", &config.client_id)
        .append_pair("client_secret", &config.client_secret)
        .append_pair("code", code)
        .append_pair("redirect_uri", &redirect_uri(ctx, config))
        .finish();
    let request = Request::post(TOKEN_URL, body)
        .header("accept", "application/json")
        .header("content-type", "application/x-www-form-urlencoded")
        .header("user-agent", USER_AGENT)
        .build();
    let response: Response = send(request).await?;
    let token: TokenResponse = serde_json::from_slice(response.body()).with_context(|| {
        format!("token endpoint returned HTTP {}", response.status())
    })?;
    match token {
        TokenResponse { access_token: Some(t), .. } => Ok(t),
        TokenResponse { error, error_description, .. } => Err(anyhow!(
            "{}",
            error_description.or(error).unwrap_or_else(|| "no access token returned".into())
        )),
    }
}

async fn fetch_user(token: &str) -> Result<GitHubUser> {
    let request = Request::get(USER_URL)
        .header("accept", "application/vnd.github+json")
        .header("authorization", format!("Bearer {token}"))
        .header("user-agent", USER_AGENT)
        .build();
    let response: Response = send(request).await?;
    if *response.status() != 200 {
        bail!("GitHub user API returned HTTP {}", response.status());
    }
    serde_json::from_slice(response.body()).context("invalid user response")
}

fn redirect_uri(ctx: &Ctx, config: &Config) -> String {
    config
        .redirect_uri
        .clone()
        .unwrap_or_else(|| format!("{}/callback", ctx.base_url))
}

fn current_session(req: &Request, config: &Config) -> Option<Session> {
    let payload = verify(&cookie(req, SESSION_COOKIE)?, config)?;
    let session: Session = serde_json::from_slice(&payload).ok()?;
    (session.exp > now()).then_some(session)
}

fn cookie(req: &Request, name: &str) -> Option<String> {
    req.header("cookie")?
        .as_str()?
        .split(';')
        .filter_map(|pair| pair.trim().split_once('='))
        .find(|(k, _)| *k == name)
        .map(|(_, v)| v.to_string())
}

fn mac(config: &Config) -> Result<Hmac<Sha256>> {
    // Session cookies are signed with a key derived from the client secret.
    let mut key = Hmac::<Sha256>::new_from_slice(b"spin-vitepress-oauth-session")
        .map_err(|e| anyhow!("{e}"))?;
    key.update(config.client_secret.as_bytes());
    Hmac::<Sha256>::new_from_slice(&key.finalize().into_bytes()).map_err(|e| anyhow!("{e}"))
}

fn sign(payload: &[u8], config: &Config) -> Result<String> {
    let encoded = B64.encode(payload);
    let mut mac = mac(config)?;
    mac.update(encoded.as_bytes());
    Ok(format!("{encoded}.{}", B64.encode(mac.finalize().into_bytes())))
}

fn verify(value: &str, config: &Config) -> Option<Vec<u8>> {
    let (encoded, signature) = value.split_once('.')?;
    let mut mac = mac(config).ok()?;
    mac.update(encoded.as_bytes());
    mac.verify_slice(&B64.decode(signature).ok()?).ok()?;
    B64.decode(encoded).ok()
}

fn random_token() -> Result<String> {
    let mut bytes = [0u8; 32];
    getrandom::fill(&mut bytes).map_err(|e| anyhow!("random: {e}"))?;
    Ok(B64.encode(bytes))
}

fn constant_time_eq(a: &[u8], b: &[u8]) -> bool {
    a.len() == b.len() && a.iter().zip(b).fold(0u8, |acc, (x, y)| acc | (x ^ y)) == 0
}

fn now() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0)
}

fn redirect(location: &str) -> Reply {
    let mut reply = Reply::new(302, "text/plain", "");
    reply.headers.push(("location".into(), location.into()));
    reply
}

fn html(status: u16, body: String) -> Reply {
    let page = format!(
        "<!doctype html><html><head><meta charset=\"utf-8\"><title>Login</title>\
         <style>body{{font-family:system-ui,sans-serif;max-width:40rem;margin:4rem auto;padding:0 1rem}}\
         img{{border-radius:50%}}</style></head><body>{body}</body></html>"
    );
    Reply::new(status, "text/html; charset=utf-8", page)
}

fn escape(s: &str) -> String {
    s.replace('&', "&amp;")
        .replace('<', "&lt;")
        .replace('>', "&gt;")
        .replace('"', "&quot;")
        .replace('\'', "&#39;")
}

const NOT_CONFIGURED: &str = "<h1>OAuth not configured</h1>\
<p>Create a GitHub OAuth App and supply its credentials as Spin variables:</p>\
<pre>SPIN_VARIABLE_OAUTH_CLIENT_ID=... SPIN_VARIABLE_OAUTH_CLIENT_SECRET=... spin up</pre>";

#[cfg(test)]
mod tests {
    use super::{base_path, is_local_path};

    #[test]
    fn return_to_must_be_a_local_path() {
        assert!(is_local_path("/"));
        assert!(is_local_path("/markdown-examples?x=1#top"));
        assert!(!is_local_path("//evil.example"));
        assert!(!is_local_path("/\\evil.example"));
        assert!(!is_local_path("https://evil.example"));
        assert!(!is_local_path("/\nfoo"));
        assert!(!is_local_path(""));
    }

    #[test]
    fn derives_mount_point_from_request_path() {
        assert_eq!(base_path("/auth", "", "/auth"), "/auth");
        assert_eq!(base_path("/auth/", "/", "/auth"), "/auth");
        assert_eq!(base_path("/auth/logout", "/logout", "/auth"), "/auth");
    }

    #[test]
    fn derives_mount_point_without_component_route_header() {
        assert_eq!(base_path("/auth/logout", "/logout", ""), "/auth");
        assert_eq!(base_path("/auth", "", ""), "/auth");
    }

    #[test]
    fn falls_back_to_component_route_when_path_is_unknown() {
        assert_eq!(base_path("", "/logout", "/auth/..."), "/auth");
        assert_eq!(base_path("", "", "/..."), "");
    }

    #[test]
    fn root_mounted_component_has_empty_base_path() {
        assert_eq!(base_path("/logout", "/logout", ""), "");
    }
}
