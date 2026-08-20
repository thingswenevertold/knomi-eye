#pragma once
#include <Arduino.h>

// Provisionnement WiFi par Bluetooth, avec une liste de reseaux connus.
//
// Les identifiants vivent en NVS, saisis depuis un telephone par BLE, jamais
// dans include/secrets.h. Ajouter un lieu ne demande donc ni cable ni
// recompilation.
//
// Pourquoi une liste et non un seul reseau : la carte se deplace entre la
// maison et le bureau. Une case unique fait qu'enregistrer l'un efface
// l'autre, et la carte revient muette du second lieu.
//
// Au demarrage, joinBest() **scanne d'abord** puis ne tente que les reseaux
// connus reellement a portee, du plus fort au plus faible. Essayer les
// reseaux en aveugle coutait plusieurs secondes de timeout par absent.
//
// SECURITE : une cle saisie ici traverse le lien BLE. Definir BLE_PASSKEY
// dans include/secrets.h avant d'utiliser ceci dans un lieu peu sur — sans
// lui le lien n'est pas chiffre.
namespace wifiprov {

// Au-dela, les plus anciens sont evinces. Six couvre maison, bureau, partage
// de connexion et quelques lieux de passage.
constexpr int MAX_NETS = 6;

void begin();

int count();
String ssidAt(int index);
bool knows(const String& ssid);

// Scanne, croise avec les reseaux connus, se connecte au plus fort present.
// Bloquant. Rend false si aucun reseau connu n'est a portee.
bool joinBest(uint32_t perNetworkTimeoutMs);

// Teste ces identifiants et, en cas de succes, les **ajoute** a la liste.
// Un SSID deja connu voit sa cle mise a jour plutot que d'etre duplique.
// En cas d'echec, la liste et la connexion en cours restent intactes.
bool join(const String& ssid, const String& pass, uint32_t timeoutMs);

// Enregistre sans tester. Sert a pre-charger un reseau hors de portee — le
// bureau depuis la maison, typiquement. Contrepartie assumee : une faute de
// frappe ne sera decouverte que sur place, alors que join() l'aurait refusee.
void remember(const String& ssid, const String& pass);

// Retire un reseau. Un ssid vide efface toute la liste.
void forget(const String& ssid);

// Scan non bloquant : lancer, sonder scanBusy(), puis lire le resultat.
void startScan();
bool scanBusy();
String scanResultJson();   // {"nets":[{"ssid":..,"rssi":..,"lock":..,"known":..}]}

String statusJson();       // {"up":..,"ssid":..,"ip":..,"rssi":..,"known":[..]}

}
