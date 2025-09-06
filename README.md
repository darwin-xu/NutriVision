# NutriVision

A lightweight prototype for food image recognition and nutrition analysis. This repository contains a Node.js server, simple front-end pages, sample images, and Arduino/hardware code to collect images/weights.

## Key components

- `server/` — Node.js Express server that accepts image uploads and returns (mock) nutrition analysis.
  - `server.js` — main server implementation and API routes.
  - `package.json` — scripts and dependencies.
  - `public/` — static front-end pages (simple demo clients).
  - `uploads/` — runtime folder where uploaded images are stored (not committed).
- `hardware/` — Arduino/embedded sketches and related notes (e.g. `ardu_cam_scale.ino`).
- `docs/` — design notes and wiring documentation.

## Features

- Accepts image uploads and a weight to produce a nutrition analysis (currently mocked).
- Simple REST API: `/api/analyze-food`, `/api/latest-analysis`, `/api/health`.
- Static demo UI under `server/public`.
- Equipment simulator (`server/equipment-simulator.js`) for testing data flows.

## Prerequisites

- Node.js 18+ (or LTS compatible)
- npm (or yarn/pnpm) for installing dependencies
- Optional: Arduino IDE or PlatformIO to load sketches in `hardware/`

## Quickstart (server)

1. Install dependencies

```bash
cd server
npm install
```

2. Run the server in development (auto-restarts with `nodemon`)

```bash
npm run dev
```

3. Or start normally

```bash
npm start
```

The server listens on the port defined by the `PORT` environment variable (default: `3000`).

## Environment

Create an `.env` file in `server/` if you want to set custom variables. Supported variables:

- `PORT` — port to run the Express server (default: `3000`).

Example `.env`:

```
PORT=3000
```

> Note: `.env` files are ignored by the repository `.gitignore` for safety.

## API

All endpoints are relative to the server root (e.g. `http://localhost:3000`).

- Health check
  - GET `/api/health`
  - Response: `{ status: 'OK', timestamp: '...' }`

- Analyze food (upload image)
  - POST `/api/analyze-food`
  - Form data (multipart/form-data):
    - `foodImage` — image file (required). Max size: 5 MB.
    - `weight` — numeric weight in grams (required).
  - Success response: JSON with `success: true` and a `data` object containing `image`, `weight`, `analysis`, and `timestamp`.

Example curl (replace path and port as needed):

```bash
curl -X POST \
  -F "foodImage=@/path/to/photo.jpg" \
  -F "weight=150" \
  http://localhost:3000/api/analyze-food
```

- Latest analysis
  - GET `/api/latest-analysis`
  - Returns the most recent analysis result produced by the server (kept in memory for this prototype).

## Uploads

Uploaded images are saved to `server/uploads/`. This folder is created automatically by the server at startup. `server/uploads/` is ignored by `.gitignore` so user data doesn't get committed.

If you want an empty uploads folder tracked in git, add a `.gitkeep` file and remove `server/uploads/` from `.gitignore`.

## Simulator

The server includes an equipment simulator useful for development and demoing flows without the physical hardware:

- `npm run simulate` — runs the simulator continuously.
- `npm run simulate-once` — runs a single simulated sample.

The simulator script is `server/equipment-simulator.js`.

## Hardware

The `hardware/` directory contains an Arduino sketch `ardu_cam_scale.ino` used to capture images and weight from an attached load cell/camera combo. See `docs/pin_connections.md` and `docs/Design.md` for wiring and design notes.

- Use the Arduino IDE or PlatformIO to upload sketches.
- The hardware side should POST image and weight to the server's `/api/analyze-food` endpoint.

## Development notes

- The server uses `multer` for uploads and returns mock nutrition data (see `server/server.js`). Replace the mock analysis with a real model or API as needed.
- Logging: `morgan` is enabled for combined logs.
- Security: `helmet` is used with some CS P and frameguard options disabled for the local dev VS Code Simple Browser; re-enable CSP for production.

## Tests & formatting

- No unit tests are provided in this prototype.
- Format code with Prettier:

```bash
npm run format
npm run format:check
```

## Contributing

Contributions are welcome. Suggested workflow:

1. Fork or branch from `main`.
2. Make changes with clear commits.
3. Run the server and simulator locally to verify behavior.
4. Open a pull request describing your changes.

## License

This repository does not include a license file. Add a `LICENSE` if you plan to open-source it.

## Contact & notes

For questions about architecture or to request features, include a GitHub issue or contact the maintainer listed in repository metadata.
