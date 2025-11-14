# spin-vitepress

A VitePress documentation site built with Spin WebAssembly components, demonstrating how to deploy static sites and serverless functions using Fermyon's Spin framework.

## Hosted Pages

- [Fermyon Wasm Functions](https://00a2a739-18a1-4f6c-857c-fe9364db866b.fwf.app)
- [Fermyon Cloud Deployment](https://hosh.ie/)

## Developers

### Prerequisites

- **WASI SDK 24**: Required for WebAssembly compilation
  ```bash
  wget https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-24/wasi-sdk-24.0-x86_64-linux.deb
  sudo dpkg -i wasi-sdk-24.0-x86_64-linux.deb
  ```

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

### Deployment

#### Deploy to Fermyon Cloud

```bash
spin cloud deploy
```

#### Deploy to Fermyon Wasm Functions

```bash
spin aka deploy
```

Requires an access token from Fermyon Wasm Functions.

