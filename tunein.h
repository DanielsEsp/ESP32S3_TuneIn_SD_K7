

// tunein.h - accès TuneIn (API OPML non officielle) pour le lecteur ESP32
// À placer dans le dossier du sketch (nouvel onglet) et à inclure dans le .ino
// à l'emplacement de l'ancien bloc "Genres et stations d'un genre" :
//     #include "tunein.h"
//
// Format des entrées stockées en PSRAM / favoris :  "cle|Nom affiché"
//   - genre   : cle = requête Browse.ashx   (ex. "id=c57941")
//   - station : cle = guide_id TuneIn        (ex. "s24861")
#pragma once

#define TI_HOST "https://opml.radiotime.com"

// ---------------------------------------------------------------------------
// HTTP GET texte (petites réponses)
// ---------------------------------------------------------------------------
static bool httpGetText(const String& url, String& body, uint32_t timeoutMs = 8000) {
  WiFiClientSecure sclient;
  WiFiClient pclient;
  HTTPClient http;
  http.setTimeout(timeoutMs);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  bool ok;
  if (url.startsWith("https")) { sclient.setInsecure(); ok = http.begin(sclient, url); }
  else                         { ok = http.begin(pclient, url); }
  if (!ok) return false;

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("HTTP %d : %s\n", code, url.c_str());
    http.end();
    return false;
  }
  body = http.getString();
  http.end();
  return true;
}

// ---------------------------------------------------------------------------
// Si l'URL est une playlist .pls / .m3u, on en extrait le premier flux
// (le .m3u8 = HLS est laissé tel quel : la bibliothèque audio le gère)
// ---------------------------------------------------------------------------
static String expandPlaylist(const String& u) {
  String lo = u;
  lo.toLowerCase();
  int q = lo.indexOf('?');
  String path = (q > 0) ? lo.substring(0, q) : lo;
  bool pls = path.endsWith(".pls");
  bool m3u = path.endsWith(".m3u");
  if (!pls && !m3u) return u;

  String body;
  if (!httpGetText(u, body, 6000)) return u;

  int i = 0;
  while (i < (int)body.length()) {
    int e = body.indexOf('\n', i);
    if (e < 0) e = body.length();
    String l = body.substring(i, e);
    l.trim();
    i = e + 1;
    if (pls) {
      String ll = l;
      ll.toLowerCase();
      if (ll.startsWith("file1=")) return l.substring(6);
    } else if (l.startsWith("http")) {
      return l;
    }
  }
  return u;
}

// ---------------------------------------------------------------------------
// guide_id (ex. "s24861") -> URL réelle du flux
// ---------------------------------------------------------------------------
bool resolveStream(const char* id, char* out, size_t n) {
  String body;
  if (!httpGetText(String(TI_HOST) + "/Tune.ashx?id=" + id, body)) return false;

  String u;
  int i = 0;
  while (i < (int)body.length()) {
    int e = body.indexOf('\n', i);
    if (e < 0) e = body.length();
    String l = body.substring(i, e);
    l.trim();
    i = e + 1;
    if (l.startsWith("http")) { u = l; break; }
  }
  if (!u.length()) { Serial.println("Tune.ashx : aucune URL"); return false; }

  u = expandPlaylist(u);
  Serial.println("flux : " + u);
  strlcpy(out, u.c_str(), n);
  return true;
}

// ---------------------------------------------------------------------------
// Navigation dans l'annuaire (Browse.ashx)
// ---------------------------------------------------------------------------
static String tiUrl(String u, bool stationsOnly) {
  if (u.startsWith("http://")) u = "https://" + u.substring(7);
  if (u.indexOf("render=") < 0) u += "&render=json";
  if (stationsOnly && u.indexOf("filter=") < 0) u += "&filter=s";   // stations uniquement (pas de podcasts)
  return u;
}

static bool addEntry(char* buf, size_t bufSize, uint32_t* off, int& count, int maxCount,
                     size_t& pos, const char* key, const char* name) {
  if (count >= maxCount) return false;
  char tmp[128];
  snprintf(tmp, sizeof(tmp), "%.40s|%.80s", key, name);
  char* first = strchr(tmp, '|');                       // le nom ne doit pas contenir de '|'
  if (first) for (char* p = first + 1; *p; p++) if (*p == '|') *p = '-';
  size_t l = strlen(tmp);
  if (pos + l + 1 > bufSize) return false;
  memcpy(buf + pos, tmp, l + 1);
  off[count++] = pos;
  pos += l + 1;
  return true;
}

// Lit une page Browse en JSON. stations=false : on garde les liens (genres) ;
// stations=true : on garde les stations (type "audio", guide_id "sXXXX")
// et on mémorise le lien "page suivante" s'il existe.
static bool browseFetch(const String& url, bool stations, char* buf, size_t bufSize,
                        uint32_t* off, int& count, int maxCount, size_t& pos, String& nextUrl) {
  nextUrl = "";
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(15000);
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  Serial.printf("TuneIn HTTP %d : %s\n", code, url.c_str());
  if (code != 200) { http.end(); return false; }

  JsonDocument filter;
  const char* ks[] = { "type", "text", "URL", "guide_id", "key" };
  for (const char* k : ks) {
    filter["body"][0][k] = true;
    filter["body"][0]["children"][0][k] = true;         // les stations sont souvent rangées dans des sections
  }
  JsonDocument doc(&spiRamAlloc);
  DeserializationError err = deserializeJson(doc, http.getStream(),
                                             DeserializationOption::Filter(filter));
  http.end();
  if (err) { Serial.printf("TuneIn JSON : %s\n", err.c_str()); return false; }

  auto handle = [&](JsonVariant v) {
    const char* type = v["type"]     | "";
    const char* text = v["text"]     | "";
    const char* u    = v["URL"]      | "";
    const char* gid  = v["guide_id"] | "";
    const char* key  = v["key"]      | "";
    if (stations) {
      if (!strcmp(type, "audio") && gid[0] == 's' && text[0])
        addEntry(buf, bufSize, off, count, maxCount, pos, gid, text);
      else if (!strcmp(type, "link") && !strcmp(key, "nextStations") && u[0])
        nextUrl = u;
    } else if (!strcmp(type, "link") && text[0]) {
      const char* q = strstr(u, "Browse.ashx?");
      if (q) addEntry(buf, bufSize, off, count, maxCount, pos, q + 12, text);
    }
  };

  for (JsonVariant v : doc["body"].as<JsonArray>()) {
    handle(v);
    for (JsonVariant c : v["children"].as<JsonArray>()) handle(c);
  }
  return true;
}

// ---------------------------------------------------------------------------
// Genres et stations d'un genre (appelés par la tâche réseau)
// ---------------------------------------------------------------------------
int cmpGenre(const void* a, const void* b) {
  return strcasecmp(entryName(genBuf + *(const uint32_t*)a),
                    entryName(genBuf + *(const uint32_t*)b));
}

bool fetchGenres() {
  genreCount = 0;
  if (!genBuf || !genOff) return false;
  size_t pos = 0;
  String next;
  if (!browseFetch(tiUrl(String(TI_HOST) + "/Browse.ashx?c=music", false), false,
                   genBuf, GENBUF_SIZE, genOff, genreCount, MAX_GENRES, pos, next)) return false;
  qsort(genOff, genreCount, sizeof(uint32_t), cmpGenre);
  Serial.printf("%d genres\n", genreCount);
  return genreCount > 0;
}

// entry = entrée de genre "id=c57941|Pop"
bool fetchGenreStations(const char* entry) {
  gsCount = 0;
  if (!gsBuf || !gsOff) return false;

  char q[80];
  entryId(entry, q, sizeof(q));
  String url = tiUrl(String(TI_HOST) + "/Browse.ashx?" + q, true);

  size_t pos = 0;
  for (int page = 0; page < 4; page++) {               // jusqu'à 4 pages de résultats
    String next;
    if (!browseFetch(url, true, gsBuf, GSBUF_SIZE, gsOff, gsCount, MAX_GS, pos, next))
      return gsCount > 0;
    if (!next.length()) break;
    url = tiUrl(next, true);
  }
  Serial.printf("%d stations pour ce genre\n", gsCount);
  return true;
}

// ---------------------------------------------------------------------------
// Logo de la station (le nom affiché est déjà connu par la liste)
// ---------------------------------------------------------------------------
void fetchStationInfo(const char* id) {
  char url[120];
  snprintf(url, sizeof(url), "http://cdn-profiles.tunein.com/%s/images/logoq.jpg", id);
  uint8_t* b = nullptr;
  size_t l = 0;
  bool ok = downloadImage(String(url), &b, &l, USE_PNG);
  if (strcmp(id, apiStation) != 0) { if (ok) free(b); return; }   // station changée entre-temps
  if (ok) publishImg(logoSlot, b, l); else publishImg(logoSlot, nullptr, 0);
}

// ---------------------------------------------------------------------------
// Titre en cours : envoyé dans le flux (ICY), signalé par la bibliothèque audio.
// ESP32-audioI2S >= 3.4.2 (dont 4.0) : callback à enregistrer dans setup() avec
//     Audio::audio_info_callback = my_audio_info;
// ---------------------------------------------------------------------------
static void tiStreamTitle(const char* info) {
  if (!info || page == PAGE_FILE) return;
  String s(info);
  s.trim();
  if (!s.length()) return;
  Serial.println("titre : " + s);
  String a = "", t = s;
  int p = s.indexOf(" - ");
  if (p > 0) { a = s.substring(0, p); t = s.substring(p + 3); }
  if (strcmp(songTitle, t.c_str()) != 0 || strcmp(songArtist, a.c_str()) != 0) songSeq++;
  strlcpy(songArtist, a.c_str(), sizeof(songArtist));
  strlcpy(songTitle,  t.c_str(), sizeof(songTitle));
  songDirty = true;
}

void my_audio_info(Audio::msg_t m) {
  switch (m.e) {
    case Audio::evt_streamtitle: tiStreamTitle(m.msg); break;
    default: break;
  }
}


//
