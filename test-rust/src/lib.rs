use spin_sdk::http::{send, EmptyBody, IntoResponse, Request, Response};
use spin_sdk::http_service;

#[http_service]
async fn handle_hello_world(_req: Request) -> anyhow::Result<impl IntoResponse> {
    // Create the outbound request object
    let outgoing = Request::get("https://random-data-api.fermyon.app/animals/json")
        .body(EmptyBody::new())
        .unwrap();

    // Send the request and await the response
    let resp: Response = send(outgoing).await?;

    Ok(resp)
}
