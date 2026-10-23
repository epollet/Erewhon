@echo off
rem Verifie que le poste est pret pour Erewhon et installe ce qui manque.
setlocal

if /i "%~1"=="help" goto help
if /i "%~1"=="-h" goto help
if /i "%~1"=="--help" goto help
if "%~1"=="/?" goto help

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\setup.ps1" %*
set "CODE=%ERRORLEVEL%"
pause
exit /b %CODE%

:help
echo Erewhon - verification / installation de l'environnement
echo.
echo   setup.bat            Verifie tout et propose de corriger ce qui manque
echo   setup.bat -Check     Verifie seulement, ne modifie rien
echo   setup.bat -Yes       Corrige sans demander confirmation
echo   setup.bat -Assets    Verifie aussi le pipeline d'assets (Python, ffmpeg, .env, Blender)
echo   setup.bat -NoBuild   Ne compile pas le module C++
echo.
echo Points verifies : Git LFS et fichiers manquants, Unreal Engine 5.8,
echo Visual Studio (MSVC + Windows SDK, via .vsconfig), compilation du module Erewhon.
exit /b 0
