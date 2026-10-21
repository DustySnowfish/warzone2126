// Warzone 2126 large map test: 512x512 tiles (four times the area of the largest classic map).
// Rolling hills with two long ridgelines and a central plateau; ground textures by height and slope.
// Geometry only: tests place units with their rules script.

const mapWidth = 512;
const mapHeight = 512;

const TEX_LOW = 0;     // sand
const TEX_MID = 5;     // ground
const TEX_HIGH = 9;    // rough
const TEX_CLIFF = 18;  // cliff face, blocking

let texture = new Array(mapWidth * mapHeight);
let height = new Array(mapWidth * mapHeight);

function hills(x, y) {
	return 120
		+ 70 * Math.sin(x * 0.031) * Math.cos(y * 0.027)
		+ 40 * Math.sin((x + y) * 0.013 + 1.7)
		+ 25 * Math.cos(x * 0.071 - y * 0.053);
}

for (let y = 0; y < mapHeight; y++) {
	for (let x = 0; x < mapWidth; x++) {
		let h = hills(x, y);
		// Two long ridgelines across the map
		const r1 = Math.abs((y - 170) - 0.35 * (x - 256));
		const r2 = Math.abs((y - 342) + 0.25 * (x - 256));
		if (r1 < 6) h += (6 - r1) * 40;
		if (r2 < 6) h += (6 - r2) * 40;
		// Central plateau
		const dc = Math.hypot(x - 256, y - 256);
		if (dc < 30) h += 220; else if (dc < 38) h += 220 * (38 - dc) / 8;
		h = Math.max(0, Math.min(500, Math.round(h)));
		height[mapWidth * y + x] = h;
		const ridge = r1 < 3 || r2 < 3 || (dc > 29 && dc < 37);
		texture[mapWidth * y + x] = ridge && (Math.abs(x - 256) > 6 && Math.abs(y - 256) > 6) ? TEX_CLIFF : h > 200 ? TEX_HIGH : h > 120 ? TEX_MID : TEX_LOW;
	}
}

setMapData(mapWidth, mapHeight, texture, height, [], [], []);
