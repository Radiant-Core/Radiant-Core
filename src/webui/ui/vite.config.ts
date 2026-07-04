import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import { VitePWA } from 'vite-plugin-pwa'

// https://vitejs.dev/config/
export default defineConfig({
  plugins: [
    react(),
    VitePWA({
      registerType: 'prompt',
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
        name: 'Radiant Core',
        short_name: 'Radiant',
        description: 'Radiant Core node wallet interface',
        theme_color: '#00f2f2',
        background_color: '#0d1117',
        display: 'standalone',
        scope: '/webui/',
        start_url: '/webui/',
        icons: [
          {
            src: 'icon-192.png',
            sizes: '192x192',
            type: 'image/png',
          },
          {
            src: 'icon-512.png',
            sizes: '512x512',
            type: 'image/png',
            purpose: 'any maskable',
          },
        ],
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
        entryFileNames:   'assets/[name].js',
        chunkFileNames:   'assets/[name].js',
        assetFileNames:   'assets/[name].[ext]',
      }
    }
  }
})
