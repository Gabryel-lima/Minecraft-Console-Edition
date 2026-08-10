#pragma once
#include <deque>

#include "LargeFeature.h"
#include "../StructureFeatureSavedData.h"

class StructureStart;

// #define ENABLE_STRUCTURE_SAVING

class StructureFeature : public LargeFeature {
public:
    // 4J added - Maps to values in the game rules xml
    enum EFeatureTypes {
        eFeature_Mineshaft,
        eFeature_NetherBridge,
        eFeature_Temples,
        eFeature_Stronghold,
        eFeature_Village,
    };

#ifdef ENABLE_STRUCTURE_SAVING
private:
    std::shared_ptr<StructureFeatureSavedData> savedData;
#endif

protected:
    std::unordered_map<int64_t, StructureStart*> cachedStructures;

    // Ordem de inserção das chaves de cachedStructures, usada só para saber
    // quem é o mais antigo na hora de podar (ver pruneCache()). Sem isto o
    // mapa cresce pra sempre: addFeature() roda a cada chunk gerado (raio de
    // 8 chunks ao redor do chunk sendo gerado, LargeFeature::apply) e nada
    // nunca removia entradas antigas antes do destructor.
    std::deque<int64_t> cacheInsertionOrder;

    // Limite de segurança: bem acima do que qualquer chamador precisa (as
    // janelas de raio 8 chunks de addFeature/postProcess se sobrepõem por só
    // ~16 chunks entre chunks vizinhos), então isto só existe pra impedir
    // crescimento sem limite numa sessão longa - não deve podar nada que
    // ainda esteja em uso pela geração corrente.
    static constexpr size_t kMaxCachedStructures = 2048;

    // Remove as entradas mais antigas de cachedStructures até o tamanho
    // voltar pro limite. Chamado a cada addFeature() bem-sucedido.
    void pruneCache();

public:
    StructureFeature();
    ~StructureFeature();

    virtual std::wstring getFeatureName() = 0;

    virtual void addFeature(Level* level, int x, int z, int xOffs, int zOffs,
                            byteArray blocks);

    bool postProcess(Level* level, Random* random, int chunkX, int chunkZ);
    bool isIntersection(int cellX, int cellZ);

    bool isInsideFeature(int cellX, int cellY, int cellZ);

protected:
    StructureStart* getStructureAt(int cellX, int cellY, int cellZ);

public:
    bool isInsideBoundingFeature(int cellX, int cellY, int cellZ);
    TilePos* getNearestGeneratedFeature(Level* level, int cellX, int cellY,
                                        int cellZ);

protected:
    std::vector<TilePos>* getGuesstimatedFeaturePositions();

private:
    virtual void restoreSavedData(Level* level);
    virtual void saveFeature(int chunkX, int chunkZ, StructureStart* feature);

    /**
     * Returns true if the given chunk coordinates should hold a structure
     * source.
     *
     * @param x
     *            chunk x
     * @param z
     *            chunk z
     * @return
     */
protected:
    virtual bool isFeatureChunk(int x, int z, bool bIsSuperflat = false) = 0;

    /**
     * Creates a new instance of a structure source at the given chunk
     * coordinates.
     *
     * @param x
     *            chunk x
     * @param z
     *            chunk z
     * @return
     */
    virtual StructureStart* createStructureStart(int x, int z) = 0;
};
