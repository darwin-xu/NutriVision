const express = require('express');
const multer = require('multer');
const cors = require('cors');
const helmet = require('helmet');
const morgan = require('morgan');
const path = require('path');
const fs = require('fs');
const os = require('os');
require('dotenv').config();

const app = express();
const PORT = process.env.PORT || 3000;
const LLM_ENDPOINT =
    process.env.LLM_ENDPOINT || 'http://127.0.0.1:1234/v1/chat/completions';
const LLM_MODEL = process.env.LLM_MODEL || 'gemma-3-27b-it';
const LLM_TIMEOUT_MS = Number(process.env.LLM_TIMEOUT_MS || 120000);
const LLM_MAX_TOKENS = Number(process.env.LLM_MAX_TOKENS || 512);

// Store the latest analysis result in memory (in production, use a database)
let latestAnalysis = null;

// Create uploads directory if it doesn't exist
const uploadsDir = path.join(__dirname, 'uploads');
if (!fs.existsSync(uploadsDir)) {
    fs.mkdirSync(uploadsDir, { recursive: true });
}

// Middleware
app.use(
    helmet({
        contentSecurityPolicy: false, // Disable CSP entirely for VS Code compatibility
        frameguard: false, // Disable X-Frame-Options to allow VS Code Simple Browser
    })
);
app.use(cors());
app.use(morgan('combined'));
app.use(express.json());
app.use(express.urlencoded({ extended: true }));

// Serve static files
app.use(express.static(path.join(__dirname, 'public')));
app.use('/uploads', express.static(path.join(__dirname, 'uploads')));

// Configure multer for file uploads
const storage = multer.diskStorage({
    destination: (req, file, cb) => {
        cb(null, 'uploads/');
    },
    filename: (req, file, cb) => {
        const uniqueSuffix = Date.now() + '-' + Math.round(Math.random() * 1e9);
        cb(
            null,
            file.fieldname +
            '-' +
            uniqueSuffix +
            path.extname(file.originalname)
        );
    },
});

const fileFilter = (req, file, cb) => {
    // Accept only image files
    if (file.mimetype.startsWith('image/')) {
        cb(null, true);
    } else {
        cb(new Error('Only image files are allowed!'), false);
    }
};

const upload = multer({
    storage: storage,
    fileFilter: fileFilter,
    limits: {
        fileSize: 5 * 1024 * 1024, // 5MB limit
    },
});

function getMimeTypeByExt(ext) {
    const e = ext.toLowerCase();
    if (e === '.jpg' || e === '.jpeg') return 'image/jpeg';
    if (e === '.png') return 'image/png';
    if (e === '.gif') return 'image/gif';
    if (e === '.svg') return 'image/svg+xml';
    if (e === '.webp') return 'image/webp';
    return 'image/jpeg';
}

async function analyzeWithLLM(imageFsPath, weight) {
    const imageBuffer = fs.readFileSync(imageFsPath);
    const base64 = imageBuffer.toString('base64');
    const mime = getMimeTypeByExt(path.extname(imageFsPath || ''));

    const userText = `You are a nutrition analysis assistant.
Given an image of food and a weight of ${weight} grams, identify the primary food item in the image and output nutrition for the given weight.

Return ONLY valid JSON in the following schema (no extra commentary):
{
  "foodType": string,
  "confidence": number, // 0..1
  "nutrition": {
    "calories": number, // kcal for the provided weight
    "protein": number,  // grams for the provided weight
    "carbs": number,    // grams for the provided weight
    "fat": number,      // grams for the provided weight
    "fiber": number,    // grams for the provided weight (if unknown, estimate or use 0)
    "GI": number,       // Glycemic Index (1-100) for the food type (if unknown, estimate or use 0)
    "GL": number,       // Glycemic Load for the provided weight (if unknown, estimate or use 0)
  },
  "healthSuggestions": string[], // 2-4 short bullet-like suggestions
  "dishSuggestions": string[] // 1-3 short suggestions for dishes that can be made with this food item
}`;

    const payload = {
        model: LLM_MODEL,
        messages: [
            {
                role: 'user',
                content: [
                    { type: 'text', text: userText },
                    {
                        type: 'image_url',
                        image_url: { url: `data:${mime};base64,${base64}` },
                    },
                ],
            },
        ],
        temperature: 0.2,
        max_tokens: LLM_MAX_TOKENS,
        stream: false,
    };

    const axios = require('axios');
    const started = Date.now();
    const { data, headers } = await axios.post(LLM_ENDPOINT, payload, {
        timeout: LLM_TIMEOUT_MS,
        timeoutErrorMessage: `LLM request timed out after ${LLM_TIMEOUT_MS}ms`,
        headers: { Accept: 'application/json', 'Content-Type': 'application/json' },
        // In case of larger base64 images
        maxBodyLength: Infinity,
        maxContentLength: Infinity,
        validateStatus: s => s >= 200 && s < 300,
    });
    const elapsed = Date.now() - started;
    if (headers && headers['content-type'] && headers['content-type'].includes('text/event-stream')) {
        console.warn('LLM responded with text/event-stream; ensure non-streaming mode is supported.');
    }
    console.log(`LLM call completed in ${elapsed}ms`);
    const raw = data?.choices?.[0]?.message?.content || '';

    // Extract JSON from response (handles fenced code blocks)
    let text = String(raw).trim();
    const fenceIdx = text.indexOf('```');
    if (fenceIdx !== -1) {
        const match = text.match(/```(?:json)?\n([\s\S]*?)```/i);
        if (match && match[1]) {
            text = match[1].trim();
        }
    }

    let parsed;
    try {
        parsed = JSON.parse(text);
    } catch (e) {
        throw new Error('LLM returned non-JSON or unparsable output');
    }

    // Basic normalization
    const suggestions = Array.isArray(parsed.healthSuggestions)
        ? parsed.healthSuggestions
        : [];

    const dishSuggestions = Array.isArray(parsed.dishSuggestions)
        ? parsed.dishSuggestions
        : [];

    const analysis = {
        foodType: String(parsed.foodType || 'Unknown'),
        confidence: Math.max(
            0,
            Math.min(1, Number(parsed.confidence ?? 0.5))
        ),
        nutrition: {
            calories: Math.round(Number(parsed.nutrition?.calories ?? 0)),
            protein: Math.round(Number(parsed.nutrition?.protein ?? 0)),
            carbs: Math.round(Number(parsed.nutrition?.carbs ?? 0)),
            fat: Math.round(Number(parsed.nutrition?.fat ?? 0)),
            fiber: Math.round(Number(parsed.nutrition?.fiber ?? 0)),
            GI: Math.round(Number(parsed.nutrition?.GI ?? 0)),
            GL: Math.round(Number(parsed.nutrition?.GL ?? 0)),
        },
        healthSuggestions: suggestions.slice(0, 4).map(s => String(s)),
        dishSuggestions: dishSuggestions.slice(0, 3).map(s => String(s)),
    };

    return analysis;
}

// Routes
app.get('/', (req, res) => {
    res.sendFile(path.join(__dirname, 'public', 'index.html'));
});

// API endpoint for food image analysis
app.post('/api/analyze-food', upload.single('foodImage'), async (req, res) => {
    try {
        if (!req.file) {
            return res.status(400).json({ success: false, error: 'No image file uploaded' });
        }

        const { weight } = req.body;
        if (!weight || isNaN(weight) || weight <= 0) {
            return res.status(400).json({ success: false, error: 'Valid weight is required' });
        }

        const uploadId = Date.now();
        const imageMeta = {
            filename: req.file.filename,
            originalName: req.file.originalname,
            size: req.file.size,
            path: `/uploads/${req.file.filename}`,
        };

        // Create processing placeholder and store as latest so UI can show immediately
        const processing = {
            id: uploadId,
            image: imageMeta,
            weight: parseFloat(weight),
            analysis: {
                foodType: 'Processing',
                confidence: 0,
                nutrition: { calories: 0, protein: 0, carbs: 0, fat: 0, fiber: 0, GI: 0, GL: 0 },
                healthSuggestions: ['Analyzing...'],
                dishSuggestions: ['Analyzing...'],
            },
            status: 'processing',
            timestamp: new Date().toISOString(),
        };
        latestAnalysis = processing;

        // Respond immediately with the processing record
        res.json({ success: true, data: processing });

        // Kick off LLM analysis in the background; do not await
        const filePathOnDisk = path.join(__dirname, req.file.path || '');
        (async () => {
            try {
                const llmAnalysis = await analyzeWithLLM(filePathOnDisk, weight);
                const completed = {
                    id: uploadId,
                    image: imageMeta,
                    weight: parseFloat(weight),
                    analysis: llmAnalysis,
                    timestamp: new Date().toISOString(),
                };
                latestAnalysis = completed;
                console.log('✅ Background analysis completed for', imageMeta.filename);
            } catch (e) {
                console.error('❌ Background LLM analysis failed:', e.message);
                const fallback = {
                    id: uploadId,
                    image: imageMeta,
                    weight: parseFloat(weight),
                    analysis: {
                        foodType: 'Unknown',
                        confidence: 0.5,
                        nutrition: { calories: Math.round((100 * weight) / 100), protein: 0, carbs: 0, fat: 0, fiber: 0, GI: 0, GL: 0 },
                        healthSuggestions: ['Unable to get detailed analysis; showing fallback values'],
                        dishSuggestions: ['No suggestion available'],
                    },
                    timestamp: new Date().toISOString(),
                };
                latestAnalysis = fallback;
            }
        })();
    } catch (error) {
        console.error('❌ Error during upload handling:', error.message);
        res.status(500).json({ success: false, error: 'Internal server error' });
    }
});

// Health check endpoint
app.get('/api/health', (req, res) => {
    res.json({ status: 'OK', timestamp: new Date().toISOString() });
});

// Get latest analysis result
app.get('/api/latest-analysis', (req, res) => {
    if (latestAnalysis) {
        res.json({
            success: true,
            data: latestAnalysis,
        });
    } else {
        res.json({
            success: false,
            error: 'No analysis data available',
        });
    }
});

// Error handling middleware
app.use((error, req, res, next) => {
    if (error instanceof multer.MulterError) {
        if (error.code === 'LIMIT_FILE_SIZE') {
            return res
                .status(400)
                .json({ error: 'File too large. Maximum size is 5MB.' });
        }
    }

    if (error.message === 'Only image files are allowed!') {
        return res.status(400).json({ error: 'Only image files are allowed.' });
    }

    console.error('❌ Internal server error:', error.message);
    res.status(500).json({ error: 'Something went wrong!' });
});

// 404 handler
app.use('*', (req, res) => {
    res.status(404).json({ error: 'Route not found' });
});

function getLocalIPv4() {
    const ifaces = os.networkInterfaces();
    for (const name of Object.keys(ifaces)) {
        for (const iface of ifaces[name] || []) {
            if (iface.family === 'IPv4' && !iface.internal) {
                return iface.address;
            }
        }
    }
    return null;
}

const server = app.listen(PORT, () => {
    const ip = getLocalIPv4();
    console.log(`🍎 Food Analysis Server running on http://localhost:${PORT}`);
    if (ip) console.log(` LAN URL: http://${ip}:${PORT}`);
    console.log(`📁 Uploads directory: ${uploadsDir}`);
});

['SIGINT', 'SIGTERM'].forEach(sig => {
    process.on(sig, () => {
        server.close(() => process.exit(0));
        // Force exit if not closed within 2s
        setTimeout(() => process.exit(0), 2000).unref();
    });
});
