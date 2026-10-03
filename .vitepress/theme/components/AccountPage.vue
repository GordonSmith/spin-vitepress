<script setup lang="ts">
import { computed, onMounted, ref } from "vue";
import { fetchUser, LOGIN_BASE, type Status, type User } from "../auth";

// The static fileserver doesn't resolve VitePress clean URLs on a full page load.
const ACCOUNT_PATH = "/account.html";

const status = ref<Status>("loading");
const user = ref<User | null>(null);
const signedOut = ref(false);

const displayName = computed(() => user.value?.name || user.value?.login || "");
const signInHref = `${LOGIN_BASE}/login?return_to=${encodeURIComponent(ACCOUNT_PATH)}`;
const signOutHref = `${LOGIN_BASE}/logout?return_to=${encodeURIComponent(`${ACCOUNT_PATH}?signed_out=1`)}`;

onMounted(async () => {
    signedOut.value = new URLSearchParams(window.location.search).get("signed_out") === "1";
    const result = await fetchUser();
    status.value = result.status;
    user.value = result.user;
});
</script>

<template>
    <div class="account">
        <p v-if="status === 'loading'" class="account-muted">Checking sign-in status…</p>

        <template v-else-if="status === 'signed-in' && user">
            <div class="account-card">
                <img v-if="user.avatar_url" class="account-avatar" :src="user.avatar_url" alt="" width="96" height="96">
                <span v-else class="account-avatar account-avatar-fallback">{{ user.login.charAt(0).toUpperCase()
                    }}</span>
                <div>
                    <h2 class="account-name">Hello, {{ displayName }}!</h2>
                    <p class="account-muted">
                        Signed in with GitHub as
                        <a v-if="user.html_url" :href="user.html_url" target="_blank" rel="noreferrer">@{{ user.login
                            }}</a>
                        <span v-else>@{{ user.login }}</span>.
                    </p>
                </div>
            </div>
            <p v-if="user.more?.bio" class="account-bio">{{ user.more.bio }}</p>
            <p><a class="account-button" :href="signOutHref" target="_self">Sign out</a></p>
        </template>

        <template v-else-if="status === 'signed-out'">
            <p v-if="signedOut">You are now signed out.</p>
            <p>You are not signed in.</p>
            <p><a class="account-button" :href="signInHref" target="_self">Sign in with GitHub</a></p>
        </template>

        <p v-else class="account-muted">Sign-in is not available on this deployment.</p>
    </div>
</template>

<style scoped>
.account-card {
    display: flex;
    align-items: center;
    gap: 20px;
    margin: 24px 0;
}

.account-avatar {
    width: 96px;
    height: 96px;
    border-radius: 50%;
    border: 1px solid var(--vp-c-divider);
    object-fit: cover;
    flex-shrink: 0;
}

.account-avatar-fallback {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    background: var(--vp-c-brand-soft);
    color: var(--vp-c-brand-1);
    font-size: 40px;
    font-weight: 600;
}

.vp-doc .account-name {
    margin: 0;
    padding: 0;
    border: none;
}

.vp-doc .account-muted {
    margin: 4px 0 0;
    color: var(--vp-c-text-2);
}

.vp-doc .account-bio {
    margin: 0 0 24px;
    font-style: italic;
    line-height: 1.7;
}

.vp-doc .account-button {
    display: inline-block;
    padding: 6px 16px;
    border: 1px solid var(--vp-c-brand-1);
    border-radius: 16px;
    color: var(--vp-c-brand-1);
    font-weight: 500;
    text-decoration: none;
    transition: color 0.25s, background-color 0.25s;
}

.vp-doc .account-button:hover {
    background-color: var(--vp-c-brand-1);
    color: var(--vp-c-white);
}

.dark .vp-doc .account-button:hover {
    color: var(--vp-c-black);
}
</style>
