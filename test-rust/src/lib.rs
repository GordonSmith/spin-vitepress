use spin_sdk::http::{send, EmptyBody, IntoResponse, Request, Response};
use spin_sdk::http_service;

#[http_service]
async fn handle_hello_world(_req: Request) -> anyhow::Result<impl IntoResponse> {
    let outgoing = Request::get("https://random-data-api.fermyon.app/animals/json")
        .body(EmptyBody::new())
        .unwrap();

    let resp: Response = send(outgoing).await?;

    Ok(resp)
}
