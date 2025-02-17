#include "providers/seventv/SeventvBadges.hpp"

#include "common/NetworkRequest.hpp"
#include "common/NetworkResult.hpp"
#include "common/Outcome.hpp"
#include "messages/Emote.hpp"
#include "messages/Image.hpp"
#include "providers/seventv/SeventvEmotes.hpp"

#include <QUrl>
#include <QUrlQuery>

#include <map>

namespace chatterino {

void SeventvBadges::initialize(Settings & /*settings*/, Paths & /*paths*/)
{
    this->loadSeventvBadges();
}

boost::optional<EmotePtr> SeventvBadges::getBadge(const UserId &id) const
{
    std::shared_lock lock(this->mutex_);

    auto it = this->badgeMap_.find(id.string);
    if (it != this->badgeMap_.end())
    {
        return it->second;
    }
    return boost::none;
}

void SeventvBadges::assignBadgeToUser(const QString &badgeID,
                                      const UserId &userID)
{
    std::unique_lock lock(this->mutex_);

    const auto badgeIt = this->knownBadges_.find(badgeID);
    if (badgeIt != this->knownBadges_.end())
    {
        this->badgeMap_[userID.string] = badgeIt->second;
    }
}

void SeventvBadges::clearBadgeFromUser(const QString &badgeID,
                                       const UserId &userID)
{
    std::unique_lock lock(this->mutex_);

    const auto it = this->badgeMap_.find(userID.string);
    if (it != this->badgeMap_.end() && it->second->id.string == badgeID)
    {
        this->badgeMap_.erase(userID.string);
    }
}

void SeventvBadges::addBadge(const QJsonObject &badgeJson)
{
    const auto badgeID = badgeJson["id"].toString();

    std::unique_lock lock(this->mutex_);

    if (this->knownBadges_.find(badgeID) != this->knownBadges_.end())
    {
        return;
    }

    auto emote = Emote{
        .name = EmoteName{},
        .images = SeventvEmotes::createImageSet(badgeJson),
        .tooltip = Tooltip{badgeJson["tooltip"].toString()},
        .homePage = Url{},
        .id = EmoteId{badgeID},
    };

    if (emote.images.getImage1()->isEmpty())
    {
        return;  // Bad images
    }

    this->knownBadges_[badgeID] =
        std::make_shared<const Emote>(std::move(emote));
}

void SeventvBadges::loadSeventvBadges()
{
    static QUrl url("https://7tv.io/v2/cosmetics");

    static QUrlQuery urlQuery;
    // valid user_identifier values: "object_id", "twitch_id", "login"
    urlQuery.addQueryItem("user_identifier", "twitch_id");

    url.setQuery(urlQuery);

    NetworkRequest(url)
        .onSuccess([this](const NetworkResult &result) -> Outcome {
            auto root = result.parseJson();

            std::unique_lock lock(this->mutex_);

            for (const auto &jsonBadge : root.value("badges").toArray())
            {
                auto badge = jsonBadge.toObject();
                auto badgeID = badge["id"].toString();
                auto urls = badge["urls"].toArray();
                auto emote = Emote{
                    .name = EmoteName{},
                    .images = ImageSet{Url{urls[0].toArray()[1].toString()},
                                       Url{urls[1].toArray()[1].toString()},
                                       Url{urls[2].toArray()[1].toString()}},
                    .tooltip = Tooltip{badge["tooltip"].toString()},
                    .homePage = Url{},
                    .id = EmoteId{badgeID},
                };

                auto emotePtr = std::make_shared<const Emote>(std::move(emote));
                this->knownBadges_[badgeID] = emotePtr;

                for (const auto &user : badge["users"].toArray())
                {
                    this->badgeMap_[user.toString()] = emotePtr;
                }
            }

            return Success;
        })
        .execute();
}

}  // namespace chatterino
