

// lecteur_sd.h - lecteur de fichiers SD (fond + galets + play + VU-mètre)
// À inclure dans radio_tunein.ino, à la place de l'ancien bloc
// "Affichage : page lecteur de fichiers (SD)" ... jusqu'à fileLoop() inclus :
//     #include "lecteur_sd.h"
// SPIFFS : /fond.jpg  /galetGauche.bmp  /galetDroit.bmp  /play.bmp
#pragma once

#define VU_SHOW_OFF   0


// =====================  À ADAPTER  =====================
// fond.jpg en 320x480 (portrait) : 1 = rotation horaire, 2 = anti-horaire. Déjà 480x320 : utilisé tel quel.
#define BG_ROT_DIR 1
// Coordonnées des galets : 0 = coin haut-gauche, 1 = centre
#define GALET_POS_IS_CENTER 0
#define GALET_BLACK_TOL  24
#define GALET_ROUND_MASK 1
#define PLAY_BLACK_TOL   24
#define GALET_MS 45                 // intervalle d'animation des galets (ms)
#define VU_ENABLED    1
#define VU_FULL_SCALE 0             // 0 = gain automatique
#define VU_SHOW_OFF   1
#define COL_FARTIST TFT_DARKGREY
#define COL_FTITLE  TFT_DARKGREY
#define COL_FBAR    TFT_DARKGREY
// ======================================================

// Zones tactiles : colonne de droite
const Zone Z_NEXT   = { 370,  28, 105, 40 };
const Zone Z_PLAY   = { 370,  70, 105, 40 };
const Zone Z_PREV   = { 370, 113, 105, 40 };
const Zone Z_VPLUS  = { 370, 153, 105, 40 };
const Zone Z_VMOINS = { 370, 190, 105, 40 };
const Zone Z_LIST   = { 370, 227, 105, 40 };
const Zone Z_RAND   = { 370, 263, 105, 40 };

// Texte (artiste + titre)
const Zone Z_ARTIST = {  40,  58, 300, 23 };
const Zone Z_TITLE  = {  40,  81, 300, 23 };

// Progression : temps écoulé | barre | durée
const Zone Z_PROG   = {  28, 192, 318, 22 };
#define PROG_TXT_W 60
#define PROG_BAR_H 10

#define F_FOND   "/fond.jpg"
#define F_GAUCHE "/galetGauche.bmp"
#define F_DROIT  "/galetDroit.bmp"
#define F_PLAY   "/play.bmp"

// Galets 69x69
#define G_SIZE 69
#define GAL_L_X 71
#define GAL_L_Y 114
#define GAL_R_X 236
#define GAL_R_Y 114
#if GALET_POS_IS_CENTER
  #define GL_X GAL_L_X
  #define GL_Y GAL_L_Y
  #define GR_X GAL_R_X
  #define GR_Y GAL_R_Y
#else
  #define GL_X (GAL_L_X + G_SIZE / 2)
  #define GL_Y (GAL_L_Y + G_SIZE / 2)
  #define GR_X (GAL_R_X + G_SIZE / 2)
  #define GR_Y (GAL_R_Y + G_SIZE / 2)
#endif
const float VIT_GAUCHE = 3.0f;
const float VIT_DROIT  = 5.0f;
#define TAMP_W 72
#define TAMP_H 72

// Icône play
#define PLAY_X 370
#define PLAY_Y 70
#define PLAY_W 105
#define PLAY_H 40

TFT_eSprite galetG = TFT_eSprite(&tft);
TFT_eSprite galetD = TFT_eSprite(&tft);
TFT_eSprite tampon = TFT_eSprite(&tft);
TFT_eSprite spritePlay = TFT_eSprite(&tft);
TFT_eSprite tamponPlay = TFT_eSprite(&tft);
bool uiOk = false;
bool playOk = false;
bool playIconShown = false;
float angG = 0, angD = 0;

// ---- VU-mètre ----
const Zone Z_VU = { 60, 280, 262, 36 };
#define VU_SEGS 32
TFT_eSprite spriteVU = TFT_eSprite(&tft);
bool vuOk = false;
bool vuIdleShown = false;
uint32_t vuLast = 0;
float vuL = 0, vuR = 0;
float vuRefL = 30, vuRefR = 30;
int vuPkL = 0, vuPkR = 0, vuHoldL = 0, vuHoldR = 0;
uint16_t vuGreen, vuYellow, vuRed, vuOff;

// --- Décodage de fond.jpg dans la PSRAM, avec rotation éventuelle ---
bool bgOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (!bgBuf) return 0;
  if (bgRot == 0) {
    for (int r = 0; r < h; r++) {
      int yy = y + r;
      if (yy < 0 || yy >= BG_H) continue;
      int cw = w;
      if (x + cw > BG_W) cw = BG_W - x;
      if (cw <= 0) continue;
      memcpy(bgBuf + yy * BG_W + x, bitmap + r * w, cw * 2);
    }
  } else {
    for (int r = 0; r < h; r++) {
      for (int c = 0; c < w; c++) {
        int sx = x + c, sy = y + r;
        int dx, dy;
        if (bgRot == 1) { dx = bgSrcH - 1 - sy; dy = sx; }
        else            { dx = sy;              dy = bgSrcW - 1 - sx; }
        if (dx >= 0 && dx < BG_W && dy >= 0 && dy < BG_H)
          bgBuf[dy * BG_W + dx] = bitmap[r * w + c];
      }
    }
  }
  return 1;
}

// --- BMP 24/32 bits (SPIFFS) -> TFT_eSprite 16 bits ---
bool loadBmpSprite(TFT_eSprite& s, const char* path, int tol) {
  File f = SPIFFS.open(path, "r");
  if (!f) { Serial.printf("BMP introuvable (SPIFFS) : %s\n", path); return false; }
  uint8_t h[54];
  if (f.read(h, 54) != 54 || h[0] != 'B' || h[1] != 'M') { f.close(); return false; }
  uint32_t off; int32_t w, ht; uint16_t bpp; uint32_t comp;
  memcpy(&off, h + 10, 4); memcpy(&w, h + 18, 4); memcpy(&ht, h + 22, 4);
  memcpy(&bpp, h + 28, 2); memcpy(&comp, h + 30, 4);
  bool bottomUp = ht > 0;
  int absH = abs(ht);
  if (w <= 0 || w > 128 || absH <= 0 || absH > 128 ||
      !(bpp == 24 || bpp == 32) || !(comp == 0 || comp == 3)) {
    Serial.printf("BMP non gere : %s (%dx%d, %u bits)\n", path, (int)w, (int)ht, (unsigned)bpp);
    f.close();
    return false;
  }
  s.setColorDepth(16);
  if (!s.createSprite(w, absH)) { f.close(); return false; }
  s.fillSprite(0);
  int bp = bpp / 8;
  uint32_t rowSize = ((bpp * w + 31) / 32) * 4;
  uint8_t row[128 * 4 + 4];
  for (int y = 0; y < absH; y++) {
    int srcRow = bottomUp ? (absH - 1 - y) : y;
    f.seek(off + (uint32_t)srcRow * rowSize);
    if (f.read(row, rowSize) != (int)rowSize) break;
    for (int x = 0; x < w; x++) {
      uint8_t b = row[x * bp], g = row[x * bp + 1], r = row[x * bp + 2];
      uint16_t c = tft.color565(r, g, b);
      if (tol > 0 && r <= tol && g <= tol && b <= tol) c = 0;
      s.drawPixel(x, y, c);
    }
  }
  f.close();
  return true;
}

void roundMask(TFT_eSprite& s) {
  int cw = s.width(), ch = s.height();
  float cx = (cw - 1) / 2.0f, cy = (ch - 1) / 2.0f;
  float rr = (cw < ch ? cw : ch) / 2.0f;
  for (int yy = 0; yy < ch; yy++)
    for (int xx = 0; xx < cw; xx++) {
      float dx = xx - cx, dy = yy - cy;
      if (dx * dx + dy * dy > rr * rr) s.drawPixel(xx, yy, 0);
    }
}

// --- Chargement unique (au premier fichier ouvert) ---
bool uiLoad() {
  if (uiOk) return true;

  if (!bgBuf) bgBuf = (uint16_t*)heap_caps_malloc(BG_W * BG_H * 2, MALLOC_CAP_SPIRAM);
  if (!bgBuf) { Serial.println("PSRAM insuffisante pour le fond"); return false; }

  File f = SPIFFS.open(F_FOND, "r");
  if (!f) { Serial.println("/fond.jpg introuvable (SPIFFS)"); return false; }
  size_t len = f.size();
  uint8_t* jb = (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
  if (!jb) { f.close(); return false; }
  f.read(jb, len);
  f.close();

  memset(bgBuf, 0, BG_W * BG_H * 2);
  uint16_t w = 0, h = 0;
  TJpgDec.getJpgSize(&w, &h, jb, len);
  bgSrcW = w; bgSrcH = h;
  bgRot = (w == 320 && h == 480) ? BG_ROT_DIR : 0;
  if (!(w == 480 && h == 320) && !(w == 320 && h == 480))
    Serial.printf("fond.jpg : %dx%d (attendu 480x320 ou 320x480)\n", w, h);

  // Le décodeur JPEG sert aussi aux logos de la radio : on change de callback le temps du fond
  TJpgDec.setJpgScale(1);
  TJpgDec.setCallback(bgOutput);
  TJpgDec.drawJpg(0, 0, jb, len);
  TJpgDec.setCallback(tft_output);
  free(jb);

  if (!loadBmpSprite(galetG, F_GAUCHE, GALET_BLACK_TOL)) return false;
  if (!loadBmpSprite(galetD, F_DROIT, GALET_BLACK_TOL))  return false;
#if GALET_ROUND_MASK
  roundMask(galetG);
  roundMask(galetD);
#endif
  galetG.setPivot(galetG.width() / 2, galetG.height() / 2);
  galetD.setPivot(galetD.width() / 2, galetD.height() / 2);
  tampon.setColorDepth(16);
  if (!tampon.createSprite(TAMP_W, TAMP_H)) return false;

  playOk = loadBmpSprite(spritePlay, F_PLAY, PLAY_BLACK_TOL);
  if (playOk) {
    int cw = spritePlay.width(), ch = spritePlay.height();
    spritePlay.setPivot(cw / 2, ch / 2);
    tamponPlay.setColorDepth(16);
    playOk = tamponPlay.createSprite(cw, ch);
    if (playOk) tamponPlay.setPivot(cw / 2, ch / 2);
  }
  if (!playOk) Serial.println("play.bmp absent : pas d'icone de lecture");

#if VU_ENABLED
  vuGreen  = tft.color565(0, 220, 60);
  vuYellow = tft.color565(240, 220, 0);
  vuRed    = tft.color565(240, 40, 20);
  vuOff    = tft.color565(30, 30, 30);
  spriteVU.setColorDepth(16);
  vuOk = spriteVU.createSprite(Z_VU.w, Z_VU.h);
  if (!vuOk) Serial.println("Sprite VU impossible : pas de vu-metre");
#endif

  uiOk = true;
  Serial.println("Interface lecteur chargee (SPIFFS)");
  return true;
}

void restoreBg(int x, int y, int w, int h) {
  if (!bgBuf) { tft.fillRect(x, y, w, h, TFT_BLACK); return; }
  for (int r = 0; r < h; r++) tft.pushImage(x, y + r, w, 1, bgBuf + (y + r) * BG_W + x);
}

void updatePlayIcon(bool force) {
  if (!uiOk || !playOk) return;
  bool want = fileOpened && !filePaused && !fileEnded && fileResult != -1;
  if (!force && want == playIconShown) return;
  playIconShown = want;
  if (want) {
    uint16_t* d = (uint16_t*)tamponPlay.getPointer();
    int tw = tamponPlay.width(), th = tamponPlay.height();
    for (int r = 0; r < th; r++)
      memcpy(d + r * tw, bgBuf + (PLAY_Y + r) * BG_W + PLAY_X, tw * 2);
    spritePlay.pushRotated(&tamponPlay, 0, 0);
    tamponPlay.pushSprite(PLAY_X, PLAY_Y);
  } else {
    restoreBg(PLAY_X, PLAY_Y, PLAY_W, PLAY_H);
  }
}

void drawGalet(TFT_eSprite& g, int cx, int cy, float ang) {
  if (!uiOk) return;
  int zx = constrain(cx - TAMP_W / 2, 0, BG_W - TAMP_W);
  int zy = constrain(cy - TAMP_H / 2, 0, BG_H - TAMP_H);
  uint16_t* d = (uint16_t*)tampon.getPointer();
  for (int r = 0; r < TAMP_H; r++)
    memcpy(d + r * TAMP_W, bgBuf + (zy + r) * BG_W + zx, TAMP_W * 2);
  tampon.setPivot(cx - zx, cy - zy);
  g.pushRotated(&tampon, (int16_t)ang, 0);
  tampon.pushSprite(zx, zy);
}

void drawGalets() {
  drawGalet(galetG, GL_X, GL_Y, angG);
  drawGalet(galetD, GR_X, GR_Y, angD);
}

#if VU_ENABLED
void drawVURow(int y, int lit, int pk) {
  const int W = Z_VU.w, H = 15;
  for (int i = 0; i < VU_SEGS; i++) {
    int x0 = (i * W) / VU_SEGS;
    int x1 = ((i + 1) * W) / VU_SEGS - 1;
    int w = x1 - x0;
    if (w < 1) w = 1;
    uint16_t c;
    if (i < VU_SEGS * 62 / 100)      c = vuGreen;
    else if (i < VU_SEGS * 85 / 100) c = vuYellow;
    else                             c = vuRed;
    if (i < lit)                     spriteVU.fillRect(x0, y, w, H, c);
    else if (pk > 0 && i == pk - 1)  spriteVU.fillRect(x0, y, w, H, TFT_DARKGREY);
    else if (VU_SHOW_OFF)            spriteVU.fillRect(x0, y, w, H, vuOff);
  }
}

void updateVU() {
  if (!uiOk || !vuOk) return;
  uint32_t now = millis();
  if (now - vuLast < 33) return;
  vuLast = now;

  bool playing = fileOpened && fileResult == 1 && !filePaused && !fileEnded;
  uint16_t raw = playing ? vuRaw : 0;
  float tl = (float)(raw >> 8);
  float tr = (float)(raw & 0xFF);

#if VU_FULL_SCALE > 0
  vuRefL = (float)VU_FULL_SCALE;
  vuRefR = (float)VU_FULL_SCALE;
#else
  vuRefL = max(16.0f, max(vuRefL * 0.9998f, tl));
  vuRefR = max(16.0f, max(vuRefR * 0.9998f, tr));
#endif

  float nl = constrain(tl / vuRefL, 0.0f, 1.0f);
  float nr = constrain(tr / vuRefR, 0.0f, 1.0f);
  vuL = (nl > vuL) ? nl : max(0.0f, vuL - 0.07f);
  vuR = (nr > vuR) ? nr : max(0.0f, vuR - 0.07f);

  int litL = (int)(vuL * VU_SEGS + 0.5f);
  int litR = (int)(vuR * VU_SEGS + 0.5f);

  if (litL >= vuPkL) { vuPkL = litL; vuHoldL = 12; } else if (vuHoldL > 0) vuHoldL--; else if (vuPkL > 0) vuPkL--;
  if (litR >= vuPkR) { vuPkR = litR; vuHoldR = 12; } else if (vuHoldR > 0) vuHoldR--; else if (vuPkR > 0) vuPkR--;

  bool idle = (litL == 0 && litR == 0 && vuPkL == 0 && vuPkR == 0);
  if (idle && vuIdleShown) return;
  vuIdleShown = idle;

  uint16_t* d = (uint16_t*)spriteVU.getPointer();
  for (int r = 0; r < Z_VU.h; r++)
    memcpy(d + r * Z_VU.w, bgBuf + (Z_VU.y + r) * BG_W + Z_VU.x, Z_VU.w * 2);
  drawVURow(2, litL, vuPkL);
  drawVURow(19, litR, vuPkR);
  spriteVU.pushSprite(Z_VU.x, Z_VU.y);
}
#else
void updateVU() {}
#endif

void drawFileFields() {
  scrollSet(scFArtist, fArtist);
  scrollSet(scFTitle, fTitle);
}

void drawFileProgress() {
  uint32_t cur = (fileResult == 1) ? audCur : 0;
  uint32_t dur = (fileResult == 1) ? audDur : 0;
  if (fileEnded && dur) cur = dur;
  if (!fpForce && cur == fpLastCur && dur == fpLastDur) return;
  fpForce = false;
  fpLastCur = cur;
  fpLastDur = dur;

  float fr = dur ? min(1.0f, (float)cur / (float)dur) : 0.0f;

  restoreBg(Z_PROG.x, Z_PROG.y, Z_PROG.w, Z_PROG.h);

  int barX = Z_PROG.x + PROG_TXT_W + 4;
  int barW = Z_PROG.w - 2 * (PROG_TXT_W + 4);
  int barY = Z_PROG.y + (Z_PROG.h - PROG_BAR_H) / 2;
  int fw = (int)(barW * fr);
  if (fw > 0) tft.fillRect(barX, barY, fw, PROG_BAR_H, COL_FBAR);

  char t1[16], t2[16];
  fmtTime(t1, sizeof(t1), cur);
  if (dur) fmtTime(t2, sizeof(t2), dur); else strlcpy(t2, "--:--", sizeof(t2));
  int cy = Z_PROG.y + Z_PROG.h / 2;
  tft.setTextSize(1);
  tft.setTextColor(TFT_BLACK);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(t1, Z_PROG.x, cy, 2);
  tft.setTextDatum(MR_DATUM);
  tft.drawString(t2, Z_PROG.x + Z_PROG.w, cy, 2);
}

void fileShow() {
  if (page != PAGE_FILE) return;
  drawFileFields();
  fpForce = true;
  drawFileProgress();
  drawGalets();
  updatePlayIcon(true);
}

void redrawFile() {
  if (bgBuf) tft.pushImage(0, 0, BG_W, BG_H, bgBuf);
  else       tft.fillScreen(TFT_BLACK);
  playIconShown = false;
  vuIdleShown = false;
  fileShow();
}

// Fichier au hasard, différent de celui en cours
int randomIdx() {
  if (recCount <= 1) return 0;
  int i;
  do { i = (int)(esp_random() % (uint32_t)recCount); } while (i == fileIdx);
  return i;
}

// Ouvre un fichier : la radio est coupée, la lecture démarre dans la tâche audio
void fileOpen(int idx) {
  if (idx < 0 || idx >= recCount) return;
  fileIdx = idx;

  // plus de radio
  streamActive = false;
  newStream = false;
  resolveReq = false;
  apiStation[0] = 0;
  curEntry[0] = 0;
  curStation = ""; curArtist = ""; curTitle = "";
  songDirty = false;

  fileNameParts(recList[idx].name, fArtist, fTitle);
  snprintf(playFilePath, sizeof(playFilePath), "/rec/%s", recList[idx].name);

  fileOpened = true;
  filePaused = false;
  fileEnded = false;
  fileFailShown = false;
  fileResult = 0;
  fileRunMs = millis();
  fpForce = true;
  playFileReq = true;                       // en dernier : la tâche audio lit les variables ci-dessus
}

void goFile(int idx) {
  if (idx < 0 || idx >= recCount) return;
  if (recWanted || recActive) {             // lecture SD et enregistrement ne se mélangent pas
    toast("Lecture impossible pendant un REC");
    drawList();
    return;
  }
  if (!uiLoad()) {
    toast("Fond / galets manquants (SPIFFS)");
    drawList();
    return;
  }
  shuffleMode = false;
  fileOpen(idx);
  angG = 0; angD = 0;
  page = PAGE_FILE;
  redrawFile();
}

// Retour à la liste : on attend l'arrêt réel de l'audio avant de toucher à la SD
void leaveFile() {
  fileOpened = false;
  filePaused = false;
  shuffleMode = false;
  playFileReq = false;
  stopReq = true;
  uint32_t t0 = millis();
  while (stopReq && millis() - t0 < 2000) delay(5);

  page = PAGE_LIST;
  redrawList();
}

// Appelé à chaque tour de loop sur la page lecteur
void fileLoop() {
  uint32_t now = millis();

  if (fileOpened) {
    if (fileResult == -1 && !fileFailShown) {
      fileFailShown = true;
      scrollSet(scFTitle, "Fichier illisible");
    } else if (fileResult == 1 && !filePaused && !fileEnded) {
      if (audio.isRunning()) fileRunMs = now;
      else if (now - fileRunMs > 2500) {                 // fin du fichier
        if (shuffleMode && recCount > 0) {
          fileOpen(randomIdx());
          fileShow();
        } else if (fileIdx + 1 < recCount) {
          fileOpen(fileIdx + 1);
          fileShow();
        } else {
          fileEnded = true;
          fpForce = true;
          drawFileProgress();
        }
      }
    }
  }

  updatePlayIcon(false);
  scrollTick(scFArtist);
  scrollTick(scFTitle);
  updateVU();

  static uint32_t lastAnim = 0;
  if (now - lastAnim >= GALET_MS) {
    lastAnim = now;
    if (fileResult == 1 && !filePaused && !fileEnded) {
      angG += VIT_GAUCHE; if (angG >= 360) angG -= 360;
      angD += VIT_DROIT;  if (angD >= 360) angD -= 360;
      drawGalets();
    }
  }

  static uint32_t lastProg = 0;
  if (now - lastProg >= 500) { lastProg = now; drawFileProgress(); }
}

// Toucher sur la page lecteur (appelé par handleTouch quand n >= 1 et waitRelease == false)
void fileTouch(int n, uint16_t x, uint16_t y, bool& waitRelease, uint32_t& last) {
  if (n >= 2) {                                        // 2 doigts : retour à la liste
    waitRelease = true;
    leaveFile();
    return;
  }
  if (millis() - last < 250) return;
  last = millis();

  if (inZone(x, y, Z_VMOINS)) {                        // volume : répétition si on maintient
    volume = max(0, volume - 1);  volDirty = true;
  } else if (inZone(x, y, Z_VPLUS)) {
    volume = min(21, volume + 1); volDirty = true;
  } else if (inZone(x, y, Z_PLAY)) {                   // lecture / pause
    waitRelease = true;
    if (fileEnded) { fileOpen(fileIdx); fileShow(); }
    else if (fileResult == 1) {
      pauseReq = true;
      filePaused = !filePaused;
    }
  } else if (inZone(x, y, Z_PREV)) {
    waitRelease = true;
    if (fileIdx > 0) { fileOpen(fileIdx - 1); fileShow(); }
  } else if (inZone(x, y, Z_NEXT)) {                   // suivant (au hasard en mode aléatoire)
    waitRelease = true;
    if (shuffleMode && recCount > 0) { fileOpen(randomIdx()); fileShow(); }
    else if (fileIdx + 1 < recCount) { fileOpen(fileIdx + 1); fileShow(); }
  } else if (inZone(x, y, Z_RAND)) {                   // lecture aléatoire
    waitRelease = true;
    if (recCount > 0) {
      shuffleMode = true;
      fileOpen(randomIdx());
      fileShow();
    }
  } else if (inZone(x, y, Z_LIST)) {
    waitRelease = true;
    leaveFile();
  }
}


//
