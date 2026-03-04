import yfinance as yf

def fiyat_getir(hisse_kodu):
    print(f"🌍 İnternete bağlanılıyor... {hisse_kodu} verisi aranıyor...")
    
    # Borsa İstanbul hisseleri için sonuna .IS eklenir
    hisse = yf.Ticker(hisse_kodu)
    
    # Günlük piyasa verisini al
    veri = hisse.history(period="1d")
    
    if not veri.empty:
        # En güncel kapanış (veya anlık) fiyatı al
        guncel_fiyat = veri['Close'].iloc[-1]
        print(f"✅ BAŞARILI! {hisse_kodu} Anlık Fiyatı: {guncel_fiyat:.2f} TL")
        return guncel_fiyat
    else:
        print(f"❌ Veri çekilemedi. {hisse_kodu} kodu yanlış olabilir veya piyasa kapalı.")
        return None

# Hadi Test Edelim!
if __name__ == "__main__":
    print("--- MEBAR AI BOT BAŞLATILIYOR ---\n")
    # ASELS (Aselsan) hissesini test ediyoruz
    fiyat_getir("ASELS.IS")