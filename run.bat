@echo off
rem Lance Erewhon hors de l'editeur (le projet doit avoir ete compile).
rem Voir "run.bat help" pour les options.
setlocal

if not defined UE_ROOT set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
set "EXE=%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor.exe"
set "PROJECT=%~dp0Erewhon.uproject"
set "OPTIONS=-game -windowed -ResX=1600 -ResY=900 -nosplash -log"

if /i "%~1"=="help" goto help
if /i "%~1"=="-h" goto help
if /i "%~1"=="--help" goto help
if "%~1"=="/?" goto help

if not exist "%EXE%" (
    echo Moteur introuvable : "%EXE%"
    echo Definissez UE_ROOT vers le dossier d'installation d'Unreal Engine 5.8.
    pause
    exit /b 1
)

if not exist "%~dp0Binaries\Win64\UnrealEditor-Erewhon.dll" (
    echo Module Erewhon non compile : lancez d'abord setup.bat.
    pause
    exit /b 1
)
if not exist "%~dp0Content\Maps\Erewhon.umap" (
    echo Carte Content\Maps\Erewhon.umap absente : lancez d'abord setup.bat.
    pause
    exit /b 1
)

if /i "%~1"=="host" goto host
if /i "%~1"=="join" goto join
if not "%~1"=="" (
    echo Option inconnue : %~1
    echo.
    goto help
)

start "" "%EXE%" "%PROJECT%" %OPTIONS%
exit /b 0

:host
start "" "%EXE%" "%PROJECT%" /Game/Maps/Erewhon?listen %OPTIONS%
exit /b 0

:join
set "ADDRESS=%~2"
if "%ADDRESS%"=="" set "ADDRESS=127.0.0.1"
start "" "%EXE%" "%PROJECT%" %ADDRESS% %OPTIONS%
exit /b 0

:help
echo Erewhon - lanceur
echo.
echo   run.bat              Partie solo
echo   run.bat host         Heberge une partie (serveur en ecoute, port 7777)
echo   run.bat join [ip]    Rejoint une partie (127.0.0.1 par defaut)
echo   run.bat help         Affiche cette aide
echo.
echo Moteur utilise : "%UE_ROOT%"
echo Pour un autre emplacement : set UE_ROOT=D:\Epic\UE_5.8
exit /b 0
