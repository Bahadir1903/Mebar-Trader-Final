import warnings
from bs4 import XMLParsedAsHTMLWarning
warnings.filterwarnings("ignore", category=XMLParsedAsHTMLWarning)

import yfinance as yf
import requests
from bs4 import BeautifulSoup
from flask import Flask, request, jsonify
import urllib.parse

app = Flask(__name__)

def hisse_fiyat_getir(hisse_kodu):
    try:
        if not hisse_kodu.endswith(".IS") and "=" not in hisse_kodu:
            hisse_kodu = f"{hisse_kodu}.IS"
        hisse = yf.Ticker(hisse_kodu)
        hist = hisse.history(period="1d")
        if not hist.empty: return round(hist['Close'].iloc[-1], 2)
        return None
    except: return None

def fon_fiyat_getir(fon_kodu):
    try:
        url = f"https://www.tefas.gov.tr/FonAnaliz.aspx?FonKod={fon_kodu}"
        headers = {"User-Agent": "Mozilla/5.0"}
        response = requests.get(url, headers=headers)
        if response.status_code == 200:
            soup = BeautifulSoup(response.content, "html.parser")
            fiyat_etiketi = soup.find("ul", class_="top-list")
            if fiyat_etiketi and fiyat_etiketi.find("span"):
                return float(fiyat_etiketi.find("span").text.replace(",", "."))
        return None
    except: return None

# --- YENİ: BASİT YAPAY ZEKA DUYGU ANALİZİ (NLP) ---
def duygu_analizi(haberler):
    if not haberler: return 50
    pozitifler = ['yükseldi', 'arttı', 'kâr', 'rekor', 'büyüme', 'olumlu', 'kazandı', 'hedef', 'tavsiye', 'al', 'uçtu']
    negatifler = ['düştü', 'azaldı', 'zarar', 'olumsuz', 'kötü', 'ceza', 'çakıldı', 'sat', 'uyarı', 'risk', 'kaybetti']
    
    skor = 50 # Nötr (50) ile başlar
    for h in haberler:
        baslik = h['baslik'].lower()
        for p in pozitifler:
            if p in baslik: skor += 15
        for n in negatifler:
            if n in baslik: skor -= 15
            
    if skor > 100: skor = 100
    if skor < 0: skor = 0
    return skor

def haber_getir(kod):
    haberler = []
    try:
        arama = urllib.parse.quote(f"{kod} hisse OR {kod} fon OR piyasa")
        url = f"https://news.google.com/rss/search?q={arama}&hl=tr&gl=TR&ceid=TR:tr"
        res = requests.get(url)
        soup = BeautifulSoup(res.content, "html.parser") 
        items = soup.find_all("item", limit=3)
        for item in items:
            baslik = item.title.text if item.title else ""
            link = item.link.text if item.link else "#"
            tarih = item.pubdate.text[5:16] if item.pubdate else ""
            haberler.append({"baslik": baslik.split(" - ")[0], "link": link, "tarih": tarih})
    except: pass
    return haberler

def fiyat_bul(kod):
    kod = kod.upper().strip()
    if not kod.endswith(".IS") and "=" not in kod:
        fiyat = fon_fiyat_getir(kod)
        if fiyat: return fiyat
    return hisse_fiyat_getir(kod)

@app.route('/api/fiyat')
def api_fiyat():
    kod = request.args.get('kod')
    fiyat = fiyat_bul(kod)
    if fiyat: return jsonify({"kod": kod, "fiyat": fiyat, "durum": "basarili"})
    return jsonify({"durum": "basarisiz"}), 404

@app.route('/api/haberler')
def api_haberler():
    kod = request.args.get('kod')
    print(f"📰 C++ Haber & AI Analizi İstedi: {kod}")
    son_haberler = haber_getir(kod)
    ai_skoru = duygu_analizi(son_haberler) # AI Puanını Hesapla
    
    return jsonify({
        "kod": kod,
        "haberler": son_haberler,
        "duygu_puani": ai_skoru, # C++'a gönderiyoruz!
        "durum": "basarili"
    })

if __name__ == "__main__":
    print("\n🚀 MEBAR AI BOT v3.0 (Duygu Analizli) ÇALIŞIYOR...\n")
    app.run(host="127.0.0.1", port=5000)