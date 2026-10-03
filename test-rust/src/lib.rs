use spin_sdk::http::{send, IntoResponse, Request, Response};
use spin_sdk::http_component;

#[http_component]
async fn handle_hello_world(_req: Request) -> anyhow::Result<impl IntoResponse> {
    let outgoing = Request::get("https://random-data-api.fermyon.app/animals/json").build();

    let resp: Response = send(outgoing).await?;

    Ok(resp)
}
