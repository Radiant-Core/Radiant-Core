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
        // Precache all static assets
        globPatterns: ['**/*.{js,css,html,png,svg,ico,woff,woff2,ttf}'],
        // Serve the cached shell for any navigation under /webui/ so the
        // "Connecting to node…" screen appears even when the node is offline.
        navigateFallback: '/webui/index.html',
        navigateFallbackAllowlist: [/^\/webui(?:\/|$)/],
        runtimeCaching: [
          {
            // API calls must always hit the network — never serve from cache.
            urlPattern: /\/webui\/api\//,
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
        target: process.env.RADIANT_NODE_URL || 'http://127.0.0.1:7898',
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
