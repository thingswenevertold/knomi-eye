@echo off
rem Regenere l'art ASCII puis envoie le firmware par WiFi.
rem Double-clic depuis l'explorateur, ou "deploie" dans un terminal.

cd /d "%~dp0"

echo.
echo === 1/2 : generation de l'art ===
py -3.12 assets\gen_cat_ascii.py
if errorlevel 1 (
    echo.
    echo ECHEC de la generation : rien n'a ete envoye au boitier.
    pause
    exit /b 1
)

echo.
echo === 2/2 : compilation et envoi par WiFi ===
py -3.12 -m platformio run -e esp32dev-ota -t upload
if errorlevel 1 (
    echo.
    echo ECHEC de l'envoi. Le boitier est-il allume et sur le reseau ?
    pause
    exit /b 1
)

echo.
echo Termine. Le boitier redemarre tout seul, compte 5 secondes.
pause
