#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WINSOCKAPI_

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "implot.h"

#pragma warning(push, 0)
#include "httplib.h"
#include "json.hpp"
#pragma warning(pop)

#pragma comment(lib, "ws2_32.lib")

#include <stdio.h>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <map>
#include <thread>
#include <mutex>    
#include <atomic>   
#include <ctime>
#include <chrono>
#include <GLFW/glfw3.h>

using namespace std;
using json = nlohmann::json;

struct Islem { string tarih; string tur; string fonAdi; double fiyat; double adet; };
struct Haber { string baslik; string link; string tarih; };

vector<Islem>        portfoy;
map<string, float>   piyasaFiyatlari;
vector<Haber>        guncelHaberler;
string               seciliHaberKodu = "GENEL";
int                  guncelDuyguPuani = 50;

std::mutex appMutex;
std::atomic<bool> isUpdating(false);
std::atomic<bool> serverCanlimi(false);

bool   otoYenile = false;
float  otoYenileSayaci = 0.0f;
string sonGuncellemeSaati = "Henuz Guncellenmedi";

static float hedefKar = 500.0f;
static float stopLoss = -200.0f;

constexpr int MAX_DATA_POINTS = 500;
static float zamanData[MAX_DATA_POINTS] = {};
static float karData[MAX_DATA_POINTS] = {};
static int   dataOffset = 0;
static float zamanSayaci = 0.0f;

static void ServerDurumKontrol()
{
    while (true) {
        httplib::Client cli("http://127.0.0.1:5000");
        cli.set_connection_timeout(1);
        auto res = cli.Get("/api/fiyat?kod=TEST");
        serverCanlimi = (res != nullptr);
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
}

static void DosyayiGuncelle()
{
    ofstream dosya("mebar_data.txt", ios::trunc);
    if (!dosya.is_open()) return;
    for (const auto& i : portfoy)
        dosya << i.tarih << " " << i.tur << " " << i.fonAdi
        << " " << i.fiyat << " " << i.adet << "\n";
}

static void VerileriYukle()
{
    portfoy.clear();
    ifstream dosya("mebar_data.txt");
    string t, tur, ad; double f, a;
    while (dosya >> t >> tur >> ad >> f >> a) {
        portfoy.push_back({ t, tur, ad, f, a });
        if (piyasaFiyatlari.find(ad) == piyasaFiyatlari.end())
            piyasaFiyatlari[ad] = (float)f;
    }
}

static void VeriEkle(const string& t, const string& tur,
    const string& ad, double f, double a)
{
    portfoy.push_back({ t, tur, ad, f, a });
    piyasaFiyatlari[ad] = (float)f;
    DosyayiGuncelle();
}

static void VeriSil(int index)
{
    if (index >= 0 && index < (int)portfoy.size()) {
        portfoy.erase(portfoy.begin() + index);
        DosyayiGuncelle();
    }
}

static string SimdikiSaatiGetir()
{
    time_t simdi = time(nullptr);
    tm* ltm = localtime(&simdi);
    char buf[16];
    sprintf(buf, "%02d:%02d:%02d", ltm->tm_hour, ltm->tm_min, ltm->tm_sec);
    return buf;
}

static void HaberleriGuncelle(string kod)
{
    httplib::Client cli("http://127.0.0.1:5000");
    cli.set_connection_timeout(2);
    auto res = cli.Get(("/api/haberler?kod=" + kod).c_str());
    if (!res || res->status != 200) return;
    try {
        json j = json::parse(res->body);
        if (j["durum"] == "basarili") {
            vector<Haber> geciciHaberler;
            int geciciPuan = j.contains("duygu_puani") ? (int)j["duygu_puani"] : 50;
            for (auto& h : j["haberler"]) {
                geciciHaberler.push_back({ h["baslik"], h["link"], h["tarih"] });
            }

            std::lock_guard<std::mutex> lock(appMutex);
            guncelHaberler = geciciHaberler;
            seciliHaberKodu = kod;
            guncelDuyguPuani = geciciPuan;
        }
    }
    catch (...) {}
}

static void PiyasayiGuncelleThread()
{
    isUpdating = true;
    try {
        httplib::Client cli("http://127.0.0.1:5000");
        cli.set_connection_timeout(2);

        vector<string> kodlar;
        {
            std::lock_guard<std::mutex> lock(appMutex);
            for (auto const& [k, v] : piyasaFiyatlari) kodlar.push_back(k);
        }

        map<string, float> yeniFiyatlar;
        for (const auto& kod : kodlar) {
            auto res = cli.Get(("/api/fiyat?kod=" + kod).c_str());
            if (res && res->status == 200) {
                try {
                    json j = json::parse(res->body);
                    if (j["durum"] == "basarili") yeniFiyatlar[kod] = j["fiyat"];
                }
                catch (...) {}
            }
            this_thread::sleep_for(chrono::milliseconds(50));
        }

        {
            std::lock_guard<std::mutex> lock(appMutex);
            for (auto const& [k, v] : yeniFiyatlar) piyasaFiyatlari[k] = v;
            sonGuncellemeSaati = SimdikiSaatiGetir();
        }
    }
    catch (...) {}

    isUpdating = false;
}

static void ProfesyonelTemaYukle()
{
    ImGui::StyleColorsLight();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0.0f; s.ChildRounding = 4.0f; s.FrameRounding = 4.0f;
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    c[ImGuiCol_ChildBg] = ImVec4(0.96f, 0.96f, 0.97f, 1.00f);
    c[ImGuiCol_Button] = ImVec4(0.00f, 0.47f, 0.84f, 1.00f);
    c[ImGuiCol_Header] = ImVec4(0.90f, 0.92f, 0.95f, 1.00f);
}s

static void glfw_error_callback(int error, const char* desc) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, desc);
}

int main(int, char**)
{
 

    srand((unsigned int)time(nullptr));
    VerileriYukle();
    thread(ServerDurumKontrol).detach();

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(1280, 850, "Mebar Professional Trader v7.1 (Final Build)", nullptr, nullptr);
    if (!window) return 1;

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO(); (void)io;

    // EMOJILER TAMAMEN SILINDI, SADECE STANDART FONT KALDI
    ImFont* fontNormal = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 18.0f, nullptr, io.Fonts->GetGlyphRangesDefault());
    ImFont* fontBuyuk = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeuib.ttf", 32.0f, nullptr, io.Fonts->GetGlyphRangesDefault());
    if (!fontNormal) io.Fonts->AddFontDefault();

    ProfesyonelTemaYukle();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    char bufTarih[64] = "2026-03-03";
    char bufAd[64] = "";
    float bufFiyat = 0.0f, bufAdet = 0.0f;
    static int seciliTur = 0;

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (otoYenile && !isUpdating) {
            otoYenileSayaci += io.DeltaTime;
            if (otoYenileSayaci >= 5.0f) {
                thread(PiyasayiGuncelleThread).detach();
                otoYenileSayaci = 0.0f;
            }
        }

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Main", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

        double toplamYatirim = 0, guncelDeger = 0;
        vector<const char*> etiketler;
        vector<double>      degerler;
        map<string, double> varlikBazliDeger;
        static vector<string> etiketDeposu;
        etiketDeposu.clear();

        appMutex.lock();
        for (const auto& i : portfoy) {
            toplamYatirim += i.fiyat * i.adet;
            float anlik = piyasaFiyatlari[i.fonAdi];
            guncelDeger += anlik * i.adet;
            varlikBazliDeger[i.fonAdi] += anlik * i.adet;
        }
        appMutex.unlock();

        double karZarar = guncelDeger - toplamYatirim;
        double karYuzde = (toplamYatirim > 0) ? (karZarar / toplamYatirim * 100.0) : 0.0;

        zamanSayaci += io.DeltaTime;
        if (zamanSayaci > 0.05f) {
            zamanData[dataOffset] = (float)glfwGetTime();
            karData[dataOffset] = (float)karZarar;
            dataOffset = (dataOffset + 1) % MAX_DATA_POINTS;
            zamanSayaci = 0;
        }

        for (auto const& [key, val] : varlikBazliDeger) {
            etiketDeposu.push_back(key);
            etiketler.push_back(etiketDeposu.back().c_str());
            degerler.push_back(val);
        }

        // --- EN UST: SERVER DURUMU ---
        {
            ImVec4 rk = serverCanlimi ? ImVec4(0, 0.75f, 0, 1) : ImVec4(0.9f, 0, 0, 1);
            const char* msg = serverCanlimi ? "[AKTIF] Python AI Bot: Calisiyor" : "[KAPALI] Python AI Bot: Baglanti Bekleniyor...";
            ImGui::TextColored(rk, "%s", msg);
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 220);
            ImGui::TextDisabled("Son Guncelleme: %s", sonGuncellemeSaati.c_str());
        }
        ImGui::Separator();

        // UST PANEL
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 1), "FINANSAL OZET");
        ImGui::Separator();
        ImGui::Columns(4, "Dashboard", false);

        ImGui::Text("TOPLAM YATIRIM");
        ImGui::PushFont(fontBuyuk);
        ImGui::Text("%.2f TL", toplamYatirim);
        ImGui::PopFont();
        ImGui::NextColumn();

        ImGui::Text("GUNCEL DEGER");
        ImGui::PushFont(fontBuyuk);
        ImGui::TextColored(ImVec4(0, 0.47f, 0.84f, 1), "%.2f TL", guncelDeger);
        ImGui::PopFont();
        ImGui::NextColumn();

        ImVec4 kr = karZarar >= 0 ? ImVec4(0, 0.7f, 0, 1) : ImVec4(0.9f, 0, 0, 1);
        ImGui::Text("NET KAR/ZARAR");
        ImGui::PushFont(fontBuyuk);
        ImGui::TextColored(kr, "%+.2f TL", karZarar);
        ImGui::PopFont();
        ImGui::NextColumn();

        ImGui::Text("GETIRI ORANI");
        ImGui::PushFont(fontBuyuk);
        ImGui::TextColored(kr, "%+.2f%%", karYuzde);
        ImGui::PopFont();

        ImGui::Columns(1);
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        // ORTA PANEL
        float kalanY = ImGui::GetContentRegionAvail().y - 220;

        // --- SOL PANEL ---
        ImGui::BeginChild("SolPanel", ImVec2(500, kalanY), true);
        ImGui::TextColored(ImVec4(0.2f, 0.2f, 0.25f, 1), "YENI ISLEM");

        ImGui::RadioButton("Fon", &seciliTur, 0); ImGui::SameLine();
        ImGui::RadioButton("Hisse", &seciliTur, 1); ImGui::SameLine();
        ImGui::RadioButton("Doviz", &seciliTur, 2);

        ImGui::PushItemWidth(350);
        ImGui::InputText("Tarih", bufTarih, 64);
        ImGui::InputText("Kod", bufAd, 64);
        ImGui::InputFloat("Fiyat", &bufFiyat, 0, 0, "%.2f");
        ImGui::InputFloat("Adet", &bufAdet, 0, 0, "%.0f");
        ImGui::PopItemWidth();

        if (ImGui::Button("KAYDET", ImVec2(350, 30))) {
            const char* turStrs[] = { "Fon", "Hisse", "Diger" };
            appMutex.lock();
            VeriEkle(bufTarih, turStrs[seciliTur], bufAd, bufFiyat, bufAdet);
            appMutex.unlock();

            bufFiyat = 0.0f;
            bufAdet = 0.0f;
            bufAd[0] = '\0';
        }

        ImGui::Spacing(); ImGui::Separator();

        if (isUpdating) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            ImGui::Button("Veriler Cekiliyor...", ImVec2(170, 30));
            ImGui::PopStyleColor();
        }
        else {
            if (ImGui::Button("PIYASAYI GUNCELLE", ImVec2(170, 30)))
                thread(PiyasayiGuncelleThread).detach();
        }

        ImGui::SameLine();
        ImGui::Checkbox("Oto-Akis", &otoYenile);
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.6f, 0.1f, 1));
        if (ImGui::Button("EXCEL'E AKTAR", ImVec2(120, 30))) {
            ofstream csv("mebar_rapor.csv");
            csv << "Tarih;Tur;Kod;Alis Fiyati;Adet;Toplam Guncel Deger\n";
            appMutex.lock();
            for (auto& p : portfoy) {
                float anlik = piyasaFiyatlari[p.fonAdi];
                csv << p.tarih << ";" << p.tur << ";" << p.fonAdi
                    << ";" << p.fiyat << ";" << p.adet
                    << ";" << (anlik * p.adet) << "\n";
            }
            appMutex.unlock();
            csv.close();
            ShellExecuteA(nullptr, "open", "mebar_rapor.csv", nullptr, nullptr, SW_SHOWNORMAL);
        }
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::TextDisabled("Son Guncelleme: %s", sonGuncellemeSaati.c_str());
        ImGui::Separator();

        ImGui::TextColored(ImVec4(0.2f, 0.2f, 0.25f, 1), "PORTFOY");
        if (ImGui::BeginTable("table", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 140)))
        {
            ImGui::TableSetupColumn("TUR");  ImGui::TableSetupColumn("KOD");
            ImGui::TableSetupColumn("ADET"); ImGui::TableSetupColumn("TOPLAM");
            ImGui::TableSetupColumn("HABER"); ImGui::TableSetupColumn("SIL");
            ImGui::TableHeadersRow();

            appMutex.lock();
            for (int i = 0; i < (int)portfoy.size(); i++) {
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("%s", portfoy[i].tur.c_str());
                ImGui::TableSetColumnIndex(1); ImGui::Text("%s", portfoy[i].fonAdi.c_str());
                ImGui::TableSetColumnIndex(2); ImGui::Text("%.0f", portfoy[i].adet);
                ImGui::TableSetColumnIndex(3); ImGui::Text("%.0f", portfoy[i].fiyat * portfoy[i].adet);

                ImGui::TableSetColumnIndex(4);
                if (ImGui::Button("OKU")) {
                    string kod = portfoy[i].fonAdi;
                    thread([kod]() { HaberleriGuncelle(kod); }).detach();
                }
                ImGui::TableSetColumnIndex(5);
                if (ImGui::Button("X")) VeriSil(i);
                ImGui::PopID();
            }
            appMutex.unlock();
            ImGui::EndTable();
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // --- SAG PANEL ---
        ImGui::BeginChild("SagPanel", ImVec2(0, kalanY), true);
        ImGui::TextColored(ImVec4(0.2f, 0.2f, 0.25f, 1), "VARLIK DAGILIMI");

        if (!degerler.empty()) {
            if (ImPlot::BeginPlot("##Donut", ImVec2(-1, 180), 0)) {
                ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_NoDecorations);
                ImPlot::PlotPieChart(etiketler.data(), degerler.data(), (int)degerler.size(), 0.5, 0.5, 0.4, "%.0f TL", 90);
                ImPlot::EndPlot();
            }
        }

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.2f, 0.2f, 0.25f, 1), "CANLI PERFORMANS & RISK YONETIMI");

        ImGui::PushItemWidth(100);
        ImGui::InputFloat("Hedef Kar (TL)", &hedefKar, 50.f, 100.f, "%.0f");
        ImGui::SameLine();
        ImGui::InputFloat("Stop Loss (TL)", &stopLoss, 50.f, 100.f, "%.0f");
        ImGui::PopItemWidth();

        if (ImPlot::BeginPlot("##Live", ImVec2(-1, -1))) {
            ImPlot::SetupAxes("Zaman (sn)", "Kar/Zarar", ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_AutoFit);
            ImPlot::SetupAxisLimits(ImAxis_X1, glfwGetTime() - 10.0, glfwGetTime(), ImGuiCond_Always);

            static float x[MAX_DATA_POINTS], y[MAX_DATA_POINTS];
            for (int i = 0; i < MAX_DATA_POINTS; ++i) {
                x[i] = zamanData[(dataOffset + i) % MAX_DATA_POINTS];
                y[i] = karData[(dataOffset + i) % MAX_DATA_POINTS];
            }
            ImPlot::PlotLine("Kar/Zarar", x, y, MAX_DATA_POINTS);

            double xs[2] = { glfwGetTime() - 100, glfwGetTime() + 100 };
            double ys_hedef[2] = { hedefKar, hedefKar };
            double ys_stop[2] = { stopLoss, stopLoss };
            ImPlot::PlotLine("Hedef", xs, ys_hedef, 2);
            ImPlot::PlotLine("Stop", xs, ys_stop, 2);
            ImPlot::EndPlot();
        }
        ImGui::EndChild();

        // --- ALT PANEL: HABER VE YAPAY ZEKA ---
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
        ImGui::BeginChild("HaberPanel", ImVec2(0, 0), true);

        appMutex.lock();
        ImGui::TextColored(ImVec4(0, 0.47f, 0.84f, 1), "[ MEBAR AI ANALIZI (NLP) ] -- %s Gundemi", seciliHaberKodu.c_str());

        ImVec4 barRengi = ImVec4(0.8f, 0.8f, 0.0f, 1); // sari
        if (guncelDuyguPuani > 55) barRengi = ImVec4(0.0f, 0.7f, 0.0f, 1); // yesil
        if (guncelDuyguPuani < 45) barRengi = ImVec4(0.9f, 0.0f, 0.0f, 1); // kirmizi

        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barRengi);
        char puanMetni[64];
        sprintf(puanMetni, "Yapay Zeka Pozitiflik Skoru: %%%d", guncelDuyguPuani);
        ImGui::ProgressBar(guncelDuyguPuani / 100.0f, ImVec2(-1, 0), puanMetni);
        ImGui::PopStyleColor();

        ImGui::Separator();

        if (guncelHaberler.empty()) {
            ImGui::TextDisabled("Listeden bir varligin yanindaki 'OKU' butonuna basin.");
        }
        else {
            int nIdx = 0;
            for (const auto& h : guncelHaberler) {
                ImGui::PushID(nIdx++);
                ImGui::Bullet();
                ImGui::TextWrapped("%s", h.baslik.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("[%s]", h.tarih.c_str());
                ImGui::SameLine();

                if (ImGui::SmallButton("HABERE GIT >>")) {
                    string url = h.link;
                    if (url.empty() || url == "#" || url.find("http") == string::npos) {
                        url = "https://www.google.com/search?q=" + h.baslik;
                        for (char& c : url) if (c == ' ') c = '+';
                    }
                    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
                ImGui::Spacing();
                ImGui::PopID();
            }
        }
        appMutex.unlock();
        ImGui::EndChild();

        ImGui::End();
        ImGui::Render();

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(1, 1, 1, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImPlot::DestroyContext();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}