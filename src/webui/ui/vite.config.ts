import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import { VitePWA } from 'vite-plugin-pwa'

// https://vitejs.dev/config/
export default defineConfig({
  plugins: [
    react(),
    VitePWA({
      registerType: 'autoUpdate',
      workbox: {
        // Precache all static assets so the app shell loads from cache when
        // the node is offline. The React app handles the "connecting" splash
        // via the serverReady state — no navigation fallback needed.
        globPatterns: ['**/*.{js,css,html,png,svg,ico,woff,woff2,ttf}'],
        // No runtimeCaching rule for /webui/api — Workbox must NOT intercept
        // API fetch calls at all.  Without a matching route the SW won't call
        // event.respondWith() for them and the browser fetches directly from
        // the network with no caching layer in the way.
        //
        // navigateFallbackDenylist guards the one remaining edge case: a
        // navigation request (URL typed directly in the address bar) to an API
        // path, which would otherwise be caught by the SPA app-shell fallback.
        navigateFallbackDenylist: [/^\/webui\/api(?:\/|$)/],
        // Take over immediately when a new SW installs so the old SW (which
        // had a NetworkOnly route that intercepted API calls) stops handling
        // fetches as fast as possible.  clientsClaim ensures the new SW
        // controls the current page without waiting for a navigation.
        skipWaiting: true,
        clientsClaim: true,
      },
      manifest: {
        id: '/webui/',
        name: 'Radiant Core',
        short_name: 'Radiant',
        description: 'Radiant Core node wallet interface',
        theme_color: '#00f2f2',
        background_color: '#0d1117',
        display: 'standalone',
        scope: '/webui/',
        start_url: '/webui/',
        categories: ['finance', 'utilities'],
        icons: [
          {
            src: 'icon-192.png',
            sizes: '192x192',
            type: 'image/png',
            purpose: 'any',
          },
          {
            src: 'icon-512.png',
            sizes: '512x512',
            type: 'image/png',
            purpose: 'any',
          },
          {
            src: 'icon-512.png',
            sizes: '512x512',
            type: 'image/png',
            purpose: 'maskable',
          },
        ],
        shortcuts: [
          {
            name: 'Send RXD',
            short_name: 'Send',
            description: 'Open the Send tab',
            url: '/webui/?action=send',
            icons: [{ src: 'icon-192.png', sizes: '192x192', type: 'image/png' }],
          },
          {
            name: 'Receive RXD',
            short_name: 'Receive',
            description: 'Show receiving addresses',
            url: '/webui/?action=receive',
            icons: [{ src: 'icon-192.png', sizes: '192x192', type: 'image/png' }],
          },
        ],
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        protocol_handlers: [{ protocol: 'radiant', url: '/webui/?uri=%s' }] as any,
      },
    }),
  ],
  base: '/webui/',
  server: {
    proxy: {
      '/webui/api': {
        target: process.env.RADIANT_NODE_URL || 'http://127.0.0.1:7332',
        changeOrigin: true,
      }
    }
  },
  build: {
    // Deterministic asset names so embed_assets.py can handle them consistently.
    rollupOptions: {
      output: {
        entryFileNames: 'assets/[name]-[hash].js',
        chunkFileNames: 'assets/[name]-[hash].js',
        assetFileNames: 'assets/[name]-[hash].[ext]',
      }
    }
  }
})
