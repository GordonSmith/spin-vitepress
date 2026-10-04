# spin-vitepress

A VitePress documentation site built with Spin WebAssembly components, demonstrating how to deploy static sites and serverless functions using Fermyon's Spin framework.

## Hosted Pages

| Deployment | Link |
|-----------|------|
| **Fermyon Cloud** | [https://vitepress.fermyon.app](https://vitepress.fermyon.app) |
| **Akamai Functions** | [https://94760937-2a22-48f9-ad58-6161e1cc7f86.fwf.app](https://94760937-2a22-48f9-ad58-6161e1cc7f86.fwf.app) |

## Developers

### Prerequisites

- **Rust**: With `wasm32-wasip1` target and WIT bindings
  ```bash
  rustup target add wasm32-wasip1
  cargo install wit-bindgen-cli
  ```

- **Node.js**: Version 22 or higher

- **Spin CLI**: Install from [Fermyon](https://developer.fermyon.com/spin/install)
  ```bash
  # Install Spin plugins
  spin plugin install js2wasm
  spin plugin install cloud
  spin plugin install aka
  ```

- **vcpkg**: For C/C++ dependencies (included as submodule)
  ```bash
  git submodule update --init --recursive
  ./vcpkg/bootstrap-vcpkg.sh
  ./vcpkg/vcpkg install
  ```

### Building

1. Install npm dependencies:
   ```bash
   npm ci
   ```

2. Build all WebAssembly components and VitePress site:
   ```bash
   spin build
   ```

3. Run locally:
   ```bash
   spin up
   ```

### OAuth (GitHub login)

The `test-oauth` Rust component (mounted at `/auth`) implements the GitHub OAuth 2.0
authorization-code flow:

| Route | Description |
|-------|-------------|
| `/auth` | Shows the signed-in user, or a sign-in page when signed out |
| `/auth/login` | Starts the OAuth flow (redirects to GitHub); optional `?return_to=/path` to come back to afterwards |
| `/auth/callback` | OAuth redirect target (validates `state`, exchanges the code) |
| `/auth/user` | JSON for the signed-in user (`401` when signed out) |
| `/auth/logout` | Clears the session and returns to the sign-in page (or `?return_to=/path`) |
| `/auth/admin` | Lists everyone who has signed in (only for logins in `oauth_admins`) |

The site's top nav (next to the GitHub link) shows the sign-in status via `/auth/user`: a
**Sign in** button when signed out, or the user's avatar with an account menu when signed in.
It is hidden when the OAuth component is unavailable (e.g. `vitepress dev` or not configured).

The session is an HMAC-signed, `HttpOnly` cookie; the GitHub access token is not stored.

1. Create a [GitHub OAuth App](https://github.com/settings/developers) with the
   callback URL `<your-origin>/auth/callback` (e.g. `http://127.0.0.1:3000/auth/callback`).
2. Supply its credentials as Spin variables:
   ```bash
   SPIN_VARIABLE_OAUTH_CLIENT_ID=... SPIN_VARIABLE_OAUTH_CLIENT_SECRET=... spin up
   ```
   or put them in a git-ignored `.env` file in the repo root, which `spin up` loads automatically:
   ```bash
   SPIN_VARIABLE_OAUTH_CLIENT_ID=...
   SPIN_VARIABLE_OAUTH_CLIENT_SECRET=...
   ```

Optional variables: `oauth_scope` (default `read:user`), `oauth_redirect_uri` (overrides the
callback URL derived from the request, useful behind proxies) and `oauth_admins` (comma-separated
GitHub logins allowed to view `/auth/admin`). Sign-ins are recorded in the default key-value store.

For CI deployments, set the repository secrets `OAUTH_CLOUD_CLIENT_ID` / `OAUTH_CLOUD_CLIENT_SECRET`
(Fermyon Cloud) and `OAUTH_AKA_CLIENT_ID` / `OAUTH_AKA_CLIENT_SECRET` (Fermyon Wasm Functions).
Without credentials, `/auth` returns a "not configured" page.

### Deployment

#### Deploy to Fermyon Cloud

```bash
npm run deploy-cloud
```

#### Deploy to Fermyon Wasm Functions

```bash
npm run deploy-aka
```

Requires an access token from Fermyon Wasm Functions.

Both scripts load the `.env` file (or the file named by `SPIN_ENV_FILE`) and pass every
`SPIN_VARIABLE_*` entry to the deploy as `--variable`. Extra arguments are forwarded, e.g.
`npm run deploy-aka -- --no-confirm`.

