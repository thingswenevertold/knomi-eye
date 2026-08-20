#pragma once
#include <cstdint>

// Tiny cross-module debug state, surfaced in the web admin's /api/status —
// useful when physical serial capture is unreliable (this board's case).
namespace diag {

void setButtonEvent(const char* name);
const char* getButtonEvent();

void setScreen(const char* name);
const char* getScreen();

// Cadence reelle de la boucle de rendu, en images par seconde, lissee.
// Le bus SPI plafonne a environ 11 ms par plein ecran sur cette dalle, donc
// la valeur observee ici dit si l'animation est limitee par le bus ou par le
// temps de dessin — sans quoi on ne fait que supposer.
void setFps(float value);
float getFps();

// Durees brutes de la derniere image, en microsecondes, sans lissage.
// Le lissage masquait la valeur reelle derriere une convergence lente ; ces
// trois nombres disent directement ou part le temps.
void setTimings(uint32_t drawUs, uint32_t netUs, uint32_t totalUs);
// Decoupage de la phase reseau, pour designer laquelle des trois coute.
void setNetSplit(uint32_t bleUs, uint32_t otaUs, uint32_t adminUs);
// Decoupage interne d'admin::handle : nettoyage des clients, fabrication du
// JSON, envoi. Une seule des trois coute, reste a savoir laquelle.
void setAdminSplit(uint32_t cleanUs, uint32_t jsonUs, uint32_t sendUs);
uint32_t getCleanUs();
uint32_t getJsonUs();
uint32_t getSendUs();
uint32_t getBleUs();
uint32_t getOtaUs();
uint32_t getAdminUs();
uint32_t getDrawUs();
uint32_t getNetUs();
uint32_t getTotalUs();

}
