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
        // Exclude API paths from navigation fallback so the SW never serves
        // index.html in place of a real API response (e.g. /webui/api with no
        // trailing slash, or any future path variant).
        navigateFallbackDenylist: [/^\/webui\/api/],
        runtimeCaching: [
          {
            // API calls must always hit the network — never serve from cache.
            // Function form catches /webui/api and /webui/api/* (no trailing-
            // slash edge case with a bare regex).
            urlPattern: ({ url }: { url: URL }) => url.pathname.startsWith('/webui/api'),
            handler: 'NetworkOnly',
          },
        ],
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
