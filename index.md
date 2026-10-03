---
# https://vitepress.dev/reference/default-theme-home-page
layout: home

hero:
  name: "Hoshies Homepage!"
  text: ""
  tagline: WebAssembly + µServices = ❤️
  image:
    dark: ./hoshie.png
    light: ./hoshie.png
  actions:
    - theme: brand
      text: Markdown Examples
      link: /markdown-examples
    - theme: alt
      text: API Examples
      link: /api-examples

features:
  - title: Rust Sample 🦀
    details: Simple Rust http server
    link: /rust
    target: _top
  - title: TypeScript Sample 📜
    details: Simple TypeScript http server
    link: /ts
    target: _top
  - title: C++ Sample 
    details: Simple C++ http server
    link: /c
    target: _top
  - title: GitHub Login 🔐
    details: OAuth 2.0 sign-in implemented in Rust
    link: /auth
    target: _top
---

