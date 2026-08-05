#include "../Platform/stdafx.h"
#include "../Headers/net.minecraft.world.entity.item.h"
#include "../Headers/net.minecraft.world.level.h"
#include "../Headers/net.minecraft.world.level.redstone.h"
#include "../Headers/net.minecraft.world.item.h"
#include "../Entities/Entity.h"
#include "WeightedPressurePlateTile.h"
#include "Util/AABB.h"

WeightedPressurePlateTile::WeightedPressurePlateTile(int id,
                                                     const std::wstring& tex,
                                                     Material* material,
                                                     int maxWeight)
    : BasePressurePlateTile(id, tex, material) {
    this->maxWeight = maxWeight;

    // 4J Stu - Move this from base class to use virtual function
    updateShape(getDataForSignal(Redstone::SIGNAL_MAX));
}

int WeightedPressurePlateTile::getSignalStrength(Level* level, int x, int y,
                                                 int z) {
    AABB at_bb = getSensitiveAABB(x, y, z);
    // 4J-fix: getEntitiesOfClass() aloca o vetor com new e transfere a posse
    // ao chamador; usar o resultado inline vazava um std::vector a cada
    // avaliação de sinal da placa.
    std::vector<std::shared_ptr<Entity> >* entities =
        level->getEntitiesOfClass(typeid(Entity), &at_bb);
    int weightOfEntities = entities != nullptr ? (int)entities->size() : 0;
    delete entities;
    int count = std::min(weightOfEntities, maxWeight);

    if (count <= 0) {
        return 0;
    } else {
        float pct = std::min(maxWeight, count) / (float)maxWeight;
        return Mth::ceil(pct * Redstone::SIGNAL_MAX);
    }
}

int WeightedPressurePlateTile::getSignalForData(int data) { return data; }

int WeightedPressurePlateTile::getDataForSignal(int signal) { return signal; }

int WeightedPressurePlateTile::getTickDelay(Level* level) {
    return SharedConstants::TICKS_PER_SECOND / 2;
}
