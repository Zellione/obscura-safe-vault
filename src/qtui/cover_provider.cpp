#include "cover_provider.h"

#include "vault/index.h"

CoverProvider::CoverProvider(QObject* parent)
    : QObject(parent)
{
}

std::span<const ui::CoverSpan> CoverProvider::getCovers(const vault::IndexNode* gallery)
{
    if (!gallery || gallery->type != vault::IndexNode::Type::Gallery)
        return {};

    return cache_.get(*gallery);
}

void CoverProvider::clear() noexcept
{
    cache_.clear();
}
