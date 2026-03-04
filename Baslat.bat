@echo off
chcp 65001 > NUL
title Mebar Studio - Sistem Baslatiliyor...
color 0A

echo ===================================================
echo        MEBAR TRADER V7.1 BASLATILIYOR...
echo ===================================================
echo.

:: .bat dosyasinin bulundugu ana klasoru calisma dizini yap
cd /d "%~dp0"

echo [1] Yapay Zeka Motoru (Python) uyandiriliyor...
:: Ana klasorden MebarAIBOT icine girip python'u calistir
start /MIN python MebarAIBOT\mebar_beyni.py

:: Python sunucusunun ayaga kalkmasi icin 2 saniye bekle
timeout /t 2 /nobreak > NUL

echo [2] Arayuz (C++) yukleniyor...
:: Ana klasorden x64\Release icindeki exe'yi calistir
start "" "x64\Release\MebarTrader.exe"

echo.
echo Her sey hazir! Bu siyah ekran 3 saniye icinde kapanacak...
timeout /t 3 /nobreak > NUL
exits