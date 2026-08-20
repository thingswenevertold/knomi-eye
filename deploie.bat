@echo off
rem Envoie le firmware au boitier par WiFi, sans cable ni bouton BOOT.
rem
rem   deploie          regenere l'art ASCII puis envoie
rem   deploie rapide   envoie seulement, sans regenerer l'art
rem
rem La generation dure plusieurs minutes : 168 images calculees en 960x960.
rem Elle n'est utile que si assets\gen_cat_ascii.py a change. Pour une modif
rem de code, "deploie rapide" suffit.
rem
rem Double-clic depuis l'explorateur, ou "deploie" dans un terminal.

cd /d "%~dp0"

if /i "%~1"=="rapide" goto envoi

echo.
echo === 1/2 : generation de l'art ===
py -3.12 assets\gen_cat_ascii.py
if errorlevel 1 (
    echo.
    echo ECHEC de la generation : rien n'a ete envoye au boitier.
    pause
    exit /b 1
)

:envoi
echo.
echo === compilation et envoi par WiFi ===
py -3.12 -m platformio run -e esp32dev-ota -t upload
if errorlevel 1 (
    echo.
    echo ECHEC de l'envoi.
    echo   - le boitier est-il allume et sur le meme reseau ?
    echo   - "ping zaza.local" repond-il ?
    echo   - le --auth de platformio_local.ini correspond-il a ce qui est
    echo     deja flashe sur la carte ?
    pause
    exit /b 1
)

echo.
echo Termine. Le boitier redemarre tout seul, compte 5 secondes.
pause
