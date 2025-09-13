const express = require('express');
const multer = require('multer');
const path = require('path');
const fs = require('fs');
const morgan = require('morgan');
const cors = require('cors');

const app = express();
const PORT = process.env.PORT || 3000; // separate default port to avoid conflicts

// Keep the latest uploaded image info in memory
let latestUpload = null;

// Create uploads directory if it doesn't exist
const uploadsDir = path.join(__dirname, 'uploads');
if (!fs.existsSync(uploadsDir)) {
    fs.mkdirSync(uploadsDir, { recursive: true });
}

// Middleware
app.use(cors());
app.use(morgan('combined'));
app.use(express.json());
app.use(express.urlencoded({ extended: true }));
app.use('/uploads', express.static(uploadsDir));

// Multer config (same field name as main server: 'foodImage')
const storage = multer.diskStorage({
    destination: (req, file, cb) => cb(null, uploadsDir),
    filename: (req, file, cb) => {
        const uniqueSuffix = Date.now() + '-' + Math.round(Math.random() * 1e9);
        cb(null, file.fieldname + '-' + uniqueSuffix + path.extname(file.originalname));
    },
});

const fileFilter = (req, file, cb) => {
    if (file.mimetype.startsWith('image/')) cb(null, true);
    else cb(new Error('Only image files are allowed!'), false);
};

const upload = multer({ storage, fileFilter, limits: { fileSize: 5 * 1024 * 1024 } });

// Minimal index page showing the latest uploaded image
app.get('/', (req, res) => {
    const html = `<!doctype html>
<html>
  <head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <title>Simple Upload Viewer</title>
    <style>
      body { font-family: system-ui, -apple-system, Segoe UI, Roboto, sans-serif; margin: 24px; }
      .wrap { max-width: 900px; margin: 0 auto; }
      img { max-width: 100%; height: auto; border: 1px solid #ddd; border-radius: 8px; }
      .meta { color: #555; margin: 12px 0 24px; }
      .empty { color: #888; }
      .footer { margin-top: 32px; font-size: 12px; color: #777; }
      code { background: #f6f8fa; padding: 2px 6px; border-radius: 4px; }
    </style>
  </head>
  <body>
    <div class="wrap">
      <h1>Latest Uploaded Image</h1>
      ${latestUpload ? `
        <div class="meta">Filename: <code>${latestUpload.filename}</code> • Size: ${latestUpload.size} bytes • Time: ${latestUpload.timestamp}${typeof latestUpload.weight === 'number' ? ` • Weight: ${latestUpload.weight} g` : ''
            }</div>
        <img src="${latestUpload.url}" alt="Latest upload" />
      ` : `
        <p class="empty">No image uploaded yet. POST to <code>/api/analyze-food</code> with field <code>foodImage</code>.</p>
      `}
      <div class="footer">This simple server only saves and displays the image for device testing.</div>
      <script>setTimeout(() => location.reload(), 3000);</script>
    </div>
  </body>
  </html>`;
    res.setHeader('Content-Type', 'text/html; charset=utf-8');
    res.send(html);
});

// Single API endpoint: accept image, ignore analysis, and expose it on the index page
app.post('/api/analyze-food', upload.single('foodImage'), async (req, res) => {
    try {
        if (!req.file) return res.status(400).json({ success: false, error: 'No image file uploaded' });

        let weight = undefined;
        if (req.body && req.body.weight !== undefined) {
            const w = parseFloat(req.body.weight);
            if (!Number.isNaN(w) && Number.isFinite(w) && w > 0) weight = Math.round(w);
        }

        latestUpload = {
            filename: req.file.filename,
            originalName: req.file.originalname,
            size: req.file.size,
            url: `/uploads/${req.file.filename}`,
            timestamp: new Date().toISOString(),
            weight,
        };

        res.json({ success: true, image: latestUpload });
    } catch (e) {
        console.error('Upload error:', e.message);
        res.status(500).json({ success: false, error: 'Internal error' });
    }
});

// Basic health check
app.get('/api/health', (req, res) => {
    res.json({ status: 'OK', timestamp: new Date().toISOString() });
});

// 404 JSON
app.use('*', (req, res) => res.status(404).json({ error: 'Route not found' }));

app.listen(PORT, () => {
    console.log(`🖼️  Simple Upload Server running on http://localhost:${PORT}`);
    console.log(`📁 Uploads directory: ${uploadsDir}`);
});
