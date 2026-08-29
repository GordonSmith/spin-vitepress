import { readFileSync } from "fs"
import { defineConfig } from "vitepress"

const spinManifest = readFileSync(new URL("../spin.toml", import.meta.url), "utf-8")

function readSpinApplicationVersion(manifest: string) {
    const applicationSection = manifest.match(/^\[application\]\s*$(?<body>[\s\S]*?)(?=^\[|(?![\s\S]))/m)?.groups?.body
    const version = applicationSection?.match(/^\s*version\s*=\s*"([^"]+)"\s*$/m)?.[1]

    if (!version) {
        throw new Error("Unable to read [application].version from spin.toml")
    }

    return version
}

const spinVersion = readSpinApplicationVersion(spinManifest)

// https://vitepress.dev/reference/site-config
export default defineConfig({
    title: "hosh.ie",
    description: "Pronounced ho-shee",
    themeConfig: {
        // https://vitepress.dev/reference/default-theme-config
        nav: [
            { text: "Home", link: "/" },
            { text: "Examples", link: "/markdown-examples" }
        ],

        sidebar: [
            {
                text: "Examples",
                items: [
                    { text: "Markdown Examples", link: "/markdown-examples" },
                    { text: "Runtime API Examples", link: "/api-examples" }
                ]
            }
        ],

        socialLinks: [
            { icon: "github", link: "https://github.com/GordonSmith/spin-vitepress" }
        ]
    },

    cleanUrls: true,
    srcExclude: ["vcpkg/**", "refs/**", "test-login/**"],
    transformPageData(pageData) {
        if (pageData.relativePath === "index.md") {
            pageData.frontmatter.hero.text = `Version ${spinVersion}`
        }
    },
})
