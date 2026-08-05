#include "4J_Storage.h"
#include <cstring>
#include <vector>
#include <string>

// --- Persistência de saves (port Linux) -------------------------------------
//
// No console original toda esta camada falava com o sistema de save-game da
// plataforma (containers do XContent/PS3), e neste port ela era um stub vazio:
// SaveSaveData() descartava o blob, GetSavesInfo() não listava nada. Efeito
// prático: o mundo nunca chegava ao disco e nunca aparecia no menu de carregar.
//
// Aqui implementamos um backend de arquivos comum. Layout, relativo ao
// diretório de trabalho (mesma convenção de Common/, music/ e Sound/):
//
//   saves/<id>/save.dat        blob principal (o buffer de AllocateSaveData)
//   saves/<id>/title.txt       nome de exibição, em UTF-8
//   saves/<id>/region_<N>.bin  subfiles de região (SPLIT_SAVES)
//
// A API original é assíncrona (devolve um estado e chama um callback quando
// termina). Como I/O local é rápido, fazemos tudo de forma síncrona e
// invocamos o callback na hora, mantendo os MESMOS valores de retorno que os
// stubs devolviam - assim o fluxo de controle de quem chama não muda, só passa
// a existir persistência de verdade.
#include <algorithm>
#include <cstdio>
#include <functional>
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

C4JStorage StorageManager;

static XMARKETPLACE_CONTENTOFFER_INFO s_dummyOffer = {};
static XCONTENT_DATA s_dummyContentData = {};

namespace {

const char* const kSavesRoot = "saves";

struct Subfile {
    int regionIndex = 0;
    std::vector<std::uint8_t> data;
};

// Identificador (nome de diretório) do save "atual" - o que SaveSaveData vai
// gravar. Vazio significa "ainda não decidido": ResetSaveData() limpa isto e o
// próximo save gera um id novo, que é como o jogo sinaliza "mundo novo".
std::string g_currentSaveId;
std::wstring g_currentTitle;

// Buffer entregue por AllocateSaveData. Pertence a nós (o chamador diz
// explicitamente "We do not own this, it belongs to the StorageManager").
std::uint8_t* g_saveBuffer = nullptr;
unsigned int g_saveBufferSize = 0;

// Blob lido por LoadSaveData, consumido depois via GetSaveSize/GetSaveData.
std::vector<std::uint8_t> g_loadedBlob;

std::vector<Subfile> g_subfiles;

// Resultado de GetSavesInfo. Mantido vivo porque ReturnSavesInfo() devolve o
// ponteiro e o menu de carregar o usa até chamar ClearSavesInfo().
SAVE_DETAILS g_savesDetails = {0, nullptr};

std::string ToUtf8(const std::wstring& w) {
    std::string out;
    out.reserve(w.size());
    for (wchar_t wc : w) {
        unsigned int c = (unsigned int)wc;
        if (c < 0x80) {
            out.push_back((char)c);
        } else if (c < 0x800) {
            out.push_back((char)(0xC0 | (c >> 6)));
            out.push_back((char)(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            out.push_back((char)(0xE0 | (c >> 12)));
            out.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (c & 0x3F)));
        } else {
            out.push_back((char)(0xF0 | (c >> 18)));
            out.push_back((char)(0x80 | ((c >> 12) & 0x3F)));
            out.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

std::string SavePath(const std::string& id) {
    return std::string(kSavesRoot) + "/" + id;
}

void EnsureDir(const std::string& path) { mkdir(path.c_str(), 0755); }

bool WriteWholeFile(const std::string& path, const void* data, size_t bytes) {
    FILE* f = fopen(path.c_str(), "wb");
    if (f == nullptr) return false;
    bool ok = (bytes == 0) || (fwrite(data, 1, bytes, f) == bytes);
    fclose(f);
    return ok;
}

bool ReadWholeFile(const std::string& path, std::vector<std::uint8_t>& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (f == nullptr) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) {
        fclose(f);
        return false;
    }
    out.resize((size_t)size);
    bool ok = (size == 0) || (fread(out.data(), 1, (size_t)size, f) == (size_t)size);
    fclose(f);
    return ok;
}

// Nomes de diretório dos saves existentes, em ordem estável.
std::vector<std::string> ListSaveIds() {
    std::vector<std::string> ids;
    DIR* dir = opendir(kSavesRoot);
    if (dir == nullptr) return ids;
    while (struct dirent* ent = readdir(dir)) {
        if (ent->d_name[0] == '.') continue;
        struct stat st;
        std::string path = SavePath(ent->d_name);
        if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            ids.push_back(ent->d_name);
        }
    }
    closedir(dir);
    std::sort(ids.begin(), ids.end());
    return ids;
}

// Id novo que ainda não exista em disco. Usa o relógio para não colidir entre
// mundos criados na mesma sessão.
std::string MakeUniqueSaveId() {
    for (int attempt = 0;; attempt++) {
        char buf[MAX_SAVEFILENAME_LENGTH];
        snprintf(buf, sizeof(buf), "save_%llu_%d",
                 (unsigned long long)time(nullptr), attempt);
        struct stat st;
        if (stat(SavePath(buf).c_str(), &st) != 0) return std::string(buf);
    }
}

void FreeSavesDetails() {
    if (g_savesDetails.SaveInfoA != nullptr) {
        for (int i = 0; i < g_savesDetails.iSaveC; i++) {
            free(g_savesDetails.SaveInfoA[i].thumbnailData);
        }
        delete[] g_savesDetails.SaveInfoA;
    }
    g_savesDetails.SaveInfoA = nullptr;
    g_savesDetails.iSaveC = 0;
}

}  // namespace

C4JStorage::C4JStorage() : m_pStringTable(nullptr) {}

void C4JStorage::Tick(void) {}

C4JStorage::EMessageResult C4JStorage::RequestMessageBox(
    unsigned int uiTitle, unsigned int uiText, unsigned int* uiOptionA,
    unsigned int uiOptionC, unsigned int pad,
    int (*Func)(void*, int, const C4JStorage::EMessageResult), void* lpParam,
    C4JStringTable* pStringTable, wchar_t* pwchFormatString,
    unsigned int focusButton) {
    return EMessage_ResultAccept;
}

C4JStorage::EMessageResult C4JStorage::GetMessageBoxResult() {
    return EMessage_Undefined;
}

bool C4JStorage::SetSaveDevice(int (*Func)(void*, const bool), void* lpParam,
                               bool bForceResetOfSaveDevice) {
    return true;
}

void C4JStorage::Init(unsigned int uiSaveVersion,
                      const wchar_t* pwchDefaultSaveName, char* pszSavePackName,
                      int iMinimumSaveSize,
                      int (*Func)(void*, const ESavingMessage, int),
                      void* lpParam, const char* szGroupID) {
    if (pwchDefaultSaveName != nullptr) g_currentTitle = pwchDefaultSaveName;
    EnsureDir(kSavesRoot);
}
void C4JStorage::ResetSaveData() {
    // "Call before a new save to clear out stored save file name": zerar o id
    // é o que faz o próximo SaveSaveData criar um mundo novo em vez de
    // sobrescrever o que estava carregado.
    g_currentSaveId.clear();
    free(g_saveBuffer);
    g_saveBuffer = nullptr;
    g_saveBufferSize = 0;
    g_subfiles.clear();
    g_loadedBlob.clear();
}
void C4JStorage::SetDefaultSaveNameForKeyboardDisplay(
    const wchar_t* pwchDefaultSaveName) {}
void C4JStorage::SetSaveTitle(const wchar_t* pwchDefaultSaveName) {
    g_currentTitle = (pwchDefaultSaveName != nullptr) ? pwchDefaultSaveName : L"";
}
bool C4JStorage::GetSaveUniqueNumber(int* piVal) {
    if (piVal) *piVal = (int)std::hash<std::string>{}(g_currentSaveId);
    return true;
}
bool C4JStorage::GetSaveUniqueFilename(char* pszName) {
    if (pszName == nullptr) return false;
    snprintf(pszName, MAX_SAVEFILENAME_LENGTH, "%s", g_currentSaveId.c_str());
    return !g_currentSaveId.empty();
}
void C4JStorage::SetSaveUniqueFilename(char* szFilename) {
    g_currentSaveId = (szFilename != nullptr) ? szFilename : "";
}
void C4JStorage::SetState(ESaveGameControlState eControlState,
                          int (*Func)(void*, const bool), void* lpParam) {}
void C4JStorage::SetSaveDisabled(bool bDisable) {}
bool C4JStorage::GetSaveDisabled(void) { return false; }
unsigned int C4JStorage::GetSaveSize() { return (unsigned int)g_loadedBlob.size(); }
void C4JStorage::GetSaveData(void* pvData, unsigned int* puiBytes) {
    unsigned int bytes = (unsigned int)g_loadedBlob.size();
    if (pvData != nullptr && bytes > 0) {
        memcpy(pvData, g_loadedBlob.data(), bytes);
    }
    if (puiBytes) *puiBytes = bytes;
}
void* C4JStorage::AllocateSaveData(unsigned int uiBytes) {
    // Uma alocação viva por vez: o chamador pede este buffer, escreve o save
    // comprimido dentro dele e depois chama SaveSaveData(), que é quem grava.
    free(g_saveBuffer);
    g_saveBuffer = (std::uint8_t*)malloc(uiBytes);
    g_saveBufferSize = (g_saveBuffer != nullptr) ? uiBytes : 0;
    return g_saveBuffer;
}
void C4JStorage::SetSaveImages(std::uint8_t* pbThumbnail,
                               unsigned int thumbnailBytes,
                               std::uint8_t* pbImage, unsigned int imageBytes,
                               std::uint8_t* pbTextData,
                               unsigned int textDataBytes) {}
C4JStorage::ESaveGameState C4JStorage::SaveSaveData(int (*Func)(void*,
                                                                const bool),
                                                    void* lpParam) {
    bool ok = false;
    if (g_saveBuffer != nullptr && g_saveBufferSize > 0) {
        if (g_currentSaveId.empty()) g_currentSaveId = MakeUniqueSaveId();

        EnsureDir(kSavesRoot);
        const std::string dir = SavePath(g_currentSaveId);
        EnsureDir(dir);

        // Grava o buffer inteiro que foi alocado. A API não informa quantos
        // bytes do buffer foram realmente usados, mas o formato é
        // auto-delimitado - [int versão][int tamanho descomprimido][stream
        // zlib] - então bytes extras no fim são ignorados na leitura.
        ok = WriteWholeFile(dir + "/save.dat", g_saveBuffer, g_saveBufferSize);

        const std::string title = ToUtf8(g_currentTitle);
        WriteWholeFile(dir + "/title.txt", title.data(), title.size());
    }

    // O callback é obrigatório: é ele que encadeia a gravação dos subfiles de
    // região (ConsoleSaveFileSplit::SaveSaveDataCallback -> SaveSubfiles).
    if (Func) Func(lpParam, ok);
    return ESaveGame_Idle;
}
void C4JStorage::CopySaveDataToNewSave(std::uint8_t* pbThumbnail,
                                       unsigned int cbThumbnail,
                                       wchar_t* wchNewName,
                                       int (*Func)(void* lpParam, bool),
                                       void* lpParam) {}
void C4JStorage::SetSaveDeviceSelected(unsigned int uiPad, bool bSelected) {}
bool C4JStorage::GetSaveDeviceSelected(unsigned int iPad) { return true; }
C4JStorage::ESaveGameState C4JStorage::DoesSaveExist(bool* pbExists) {
    if (pbExists) *pbExists = !ListSaveIds().empty();
    return ESaveGame_Idle;
}
bool C4JStorage::EnoughSpaceForAMinSaveGame() { return true; }
void C4JStorage::SetSaveMessageVPosition(float fY) {}
C4JStorage::ESaveGameState C4JStorage::GetSavesInfo(
    int iPad,
    int (*Func)(void* lpParam, SAVE_DETAILS* pSaveDetails, const bool),
    void* lpParam, char* pszSavePackName) {
    FreeSavesDetails();

    std::vector<std::string> ids = ListSaveIds();
    if (!ids.empty()) {
        g_savesDetails.SaveInfoA = new SAVE_INFO[ids.size()];
        memset(g_savesDetails.SaveInfoA, 0, sizeof(SAVE_INFO) * ids.size());

        for (size_t i = 0; i < ids.size(); i++) {
            SAVE_INFO& info = g_savesDetails.SaveInfoA[i];
            snprintf(info.UTF8SaveFilename, MAX_SAVEFILENAME_LENGTH, "%s",
                     ids[i].c_str());

            // Nome de exibição: title.txt se existir, senão cai para o id -
            // assim um save gravado por uma versão anterior ainda aparece.
            std::vector<std::uint8_t> title;
            if (ReadWholeFile(SavePath(ids[i]) + "/title.txt", title) &&
                !title.empty()) {
                size_t n = std::min(title.size(),
                                    (size_t)(MAX_DISPLAYNAME_LENGTH - 1));
                memcpy(info.UTF8SaveTitle, title.data(), n);
                info.UTF8SaveTitle[n] = '\0';
            } else {
                snprintf(info.UTF8SaveTitle, MAX_DISPLAYNAME_LENGTH, "%s",
                         ids[i].c_str());
            }

            struct stat st;
            if (stat((SavePath(ids[i]) + "/save.dat").c_str(), &st) == 0) {
                info.metaData.modifiedTime = st.st_mtime;
                info.metaData.dataSize = (unsigned int)st.st_size;
            }
            info.metaData.thumbnailSize = 0;
            info.thumbnailData = nullptr;
        }
        g_savesDetails.iSaveC = (int)ids.size();
    }

    if (Func) Func(lpParam, &g_savesDetails, true);
    return ESaveGame_Idle;
}
PSAVE_DETAILS C4JStorage::ReturnSavesInfo() { return &g_savesDetails; }
void C4JStorage::ClearSavesInfo() { FreeSavesDetails(); }
C4JStorage::ESaveGameState C4JStorage::LoadSaveDataThumbnail(
    PSAVE_INFO pSaveInfo,
    int (*Func)(void* lpParam, std::uint8_t* thumbnailData,
                unsigned int thumbnailBytes),
    void* lpParam) {
    return ESaveGame_Idle;
}
void C4JStorage::GetSaveCacheFileInfo(unsigned int fileIndex,
                                      XCONTENT_DATA& xContentData) {
    memset(&xContentData, 0, sizeof(xContentData));
}
void C4JStorage::GetSaveCacheFileInfo(unsigned int fileIndex,
                                      std::uint8_t** ppbImageData,
                                      unsigned int* pImageBytes) {
    if (ppbImageData) *ppbImageData = nullptr;
    if (pImageBytes) *pImageBytes = 0;
}
C4JStorage::ESaveGameState C4JStorage::LoadSaveData(
    PSAVE_INFO pSaveInfo, int (*Func)(void* lpParam, const bool, const bool),
    void* lpParam) {
    g_loadedBlob.clear();
    g_subfiles.clear();

    bool corrupt = true;
    if (pSaveInfo != nullptr) {
        const std::string id = pSaveInfo->UTF8SaveFilename;
        const std::string dir = SavePath(id);

        if (ReadWholeFile(dir + "/save.dat", g_loadedBlob) &&
            !g_loadedBlob.empty()) {
            corrupt = false;
            // Saves seguintes devem sobrescrever este mundo, não criar um novo.
            g_currentSaveId = id;

            // Carrega os subfiles de região. Quem chama (ConsoleSaveFileSplit::
            // _init) assume a posse desta memória e a libera com free(), por
            // isso GetSubfileDetails entrega blocos vindos de malloc().
            for (const std::string& entry : [&] {
                     std::vector<std::string> names;
                     DIR* d = opendir(dir.c_str());
                     if (d != nullptr) {
                         while (struct dirent* e = readdir(d)) {
                             if (strncmp(e->d_name, "region_", 7) == 0)
                                 names.push_back(e->d_name);
                         }
                         closedir(d);
                     }
                     std::sort(names.begin(), names.end());
                     return names;
                 }()) {
                Subfile sf;
                sf.regionIndex = atoi(entry.c_str() + 7);
                if (ReadWholeFile(dir + "/" + entry, sf.data)) {
                    g_subfiles.push_back(std::move(sf));
                }
            }
        }
    }

    if (Func) Func(lpParam, corrupt, true);
    return ESaveGame_Idle;
}
C4JStorage::ESaveGameState C4JStorage::DeleteSaveData(PSAVE_INFO pSaveInfo,
                                                      int (*Func)(void* lpParam,
                                                                  const bool),
                                                      void* lpParam) {
    bool ok = false;
    if (pSaveInfo != nullptr) {
        const std::string dir = SavePath(pSaveInfo->UTF8SaveFilename);
        DIR* d = opendir(dir.c_str());
        if (d != nullptr) {
            while (struct dirent* e = readdir(d)) {
                if (e->d_name[0] == '.') continue;
                unlink((dir + "/" + e->d_name).c_str());
            }
            closedir(d);
            ok = (rmdir(dir.c_str()) == 0);
        }
    }
    if (Func) Func(lpParam, ok);
    return ESaveGame_Idle;
}
void C4JStorage::RegisterMarketplaceCountsCallback(
    int (*Func)(void* lpParam, C4JStorage::DLC_TMS_DETAILS*, int),
    void* lpParam) {}
void C4JStorage::SetDLCPackageRoot(char* pszDLCRoot) {}
C4JStorage::EDLCStatus C4JStorage::GetDLCOffers(
    int iPad, int (*Func)(void*, int, std::uint32_t, int), void* lpParam,
    std::uint32_t dwOfferTypesBitmask) {
    return EDLC_NoOffers;
}
unsigned int C4JStorage::CancelGetDLCOffers() { return 0; }
void C4JStorage::ClearDLCOffers() {}
XMARKETPLACE_CONTENTOFFER_INFO& C4JStorage::GetOffer(unsigned int dw) {
    return s_dummyOffer;
}
int C4JStorage::GetOfferCount() { return 0; }
unsigned int C4JStorage::InstallOffer(int iOfferIDC, std::uint64_t* ullOfferIDA,
                                      int (*Func)(void*, int, int),
                                      void* lpParam, bool bTrial) {
    return 0;
}
unsigned int C4JStorage::GetAvailableDLCCount(int iPad) { return 0; }
C4JStorage::EDLCStatus C4JStorage::GetInstalledDLC(int iPad,
                                                   int (*Func)(void*, int, int),
                                                   void* lpParam) {
    if (Func) {
        Func(lpParam, 0, iPad);
    }
    return EDLC_NoInstalledDLC;
}
XCONTENT_DATA& C4JStorage::GetDLC(unsigned int dw) {
    return s_dummyContentData;
}
std::uint32_t C4JStorage::MountInstalledDLC(
    int iPad, std::uint32_t dwDLC,
    int (*Func)(void*, int, std::uint32_t, std::uint32_t), void* lpParam,
    const char* szMountDrive) {
    return 0;
}
unsigned int C4JStorage::UnmountInstalledDLC(const char* szMountDrive) {
    return 0;
}
void C4JStorage::GetMountedDLCFileList(const char* szMountDrive,
                                       std::vector<std::string>& fileList) {
    fileList.clear();
}
std::string C4JStorage::GetMountedPath(std::string szMount) { return ""; }
C4JStorage::ETMSStatus C4JStorage::ReadTMSFile(
    int iQuadrant, eGlobalStorage eStorageFacility,
    C4JStorage::eTMS_FileType eFileType, wchar_t* pwchFilename,
    std::uint8_t** ppBuffer, unsigned int* pBufferSize,
    int (*Func)(void*, wchar_t*, int, bool, int), void* lpParam, int iAction) {
    return ETMSStatus_Fail;
}
bool C4JStorage::WriteTMSFile(int iQuadrant, eGlobalStorage eStorageFacility,
                              wchar_t* pwchFilename, std::uint8_t* pBuffer,
                              unsigned int bufferSize) {
    return false;
}
bool C4JStorage::DeleteTMSFile(int iQuadrant, eGlobalStorage eStorageFacility,
                               wchar_t* pwchFilename) {
    return false;
}
void C4JStorage::StoreTMSPathName(wchar_t* pwchName) {}
C4JStorage::ETMSStatus C4JStorage::TMSPP_ReadFile(
    int iPad, C4JStorage::eGlobalStorage eStorageFacility,
    C4JStorage::eTMS_FILETYPEVAL eFileTypeVal, const char* szFilename,
    int (*Func)(void*, int, int, PTMSPP_FILEDATA, const char*), void* lpParam,
    int iUserData) {
    return ETMSStatus_Fail;
}
unsigned int C4JStorage::CRC(unsigned char* buf, int len) {
    unsigned int crc = 0xFFFFFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
        }
    }
    return ~crc;
}

int C4JStorage::AddSubfile(int regionIndex) {
    Subfile sf;
    sf.regionIndex = regionIndex;
    g_subfiles.push_back(std::move(sf));
    return (int)g_subfiles.size() - 1;
}
unsigned int C4JStorage::GetSubfileCount() {
    return (unsigned int)g_subfiles.size();
}
void C4JStorage::GetSubfileDetails(unsigned int i, int* regionIndex,
                                   void** data, unsigned int* size) {
    if (i >= g_subfiles.size()) {
        if (regionIndex) *regionIndex = 0;
        if (data) *data = nullptr;
        if (size) *size = 0;
        return;
    }

    Subfile& sf = g_subfiles[i];
    if (regionIndex) *regionIndex = sf.regionIndex;
    if (size) *size = (unsigned int)sf.data.size();

    // A posse passa para quem chama, que libera com free() (ver
    // ConsoleSaveFileSplit::RegionFileReference::ReleaseCompressed), então a
    // cópia tem de vir de malloc().
    if (data) {
        if (sf.data.empty()) {
            *data = nullptr;
        } else {
            void* copy = malloc(sf.data.size());
            if (copy != nullptr) memcpy(copy, sf.data.data(), sf.data.size());
            *data = copy;
        }
    }
}
void C4JStorage::ResetSubfiles() { g_subfiles.clear(); }
void C4JStorage::UpdateSubfile(int index, void* data, unsigned int size) {
    if (index < 0 || (size_t)index >= g_subfiles.size()) return;
    // Copiamos: o buffer comprimido do chamador é liberado/reaproveitado por
    // ele antes de SaveSubfiles() acontecer.
    std::vector<std::uint8_t>& dst = g_subfiles[index].data;
    dst.assign((std::uint8_t*)data, (std::uint8_t*)data + size);
}
void C4JStorage::SaveSubfiles(int (*Func)(void*, const bool), void* param) {
    bool ok = true;
    if (!g_currentSaveId.empty()) {
        const std::string dir = SavePath(g_currentSaveId);
        EnsureDir(kSavesRoot);
        EnsureDir(dir);
        for (const Subfile& sf : g_subfiles) {
            if (sf.data.empty()) continue;
            char name[64];
            snprintf(name, sizeof(name), "/region_%d.bin", sf.regionIndex);
            ok &= WriteWholeFile(dir + name, sf.data.data(), sf.data.size());
        }
    }
    if (Func) Func(param, ok);
}
C4JStorage::ESaveGameState C4JStorage::GetSaveState() { return ESaveGame_Idle; }
void C4JStorage::ContinueIncompleteOperation() {}
