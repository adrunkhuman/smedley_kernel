#include <smedley/events/dailyupdate.hpp>
#include <smedley/plugin.hpp>
#include <smedley/v2/country.hpp>
#include <smedley/v2/gamestate.hpp>

#include <fstream>
#include <limits>

namespace economy_trace
{
    class Plugin final : public smedley::Plugin
    {
    public:
        void OnLoad() override
        {
            output_.open("economy_trace.csv", std::ios::trunc);
            if (!output_) {
                logger().Failure("cannot open economy_trace.csv in the game directory");
                return;
            }
            output_ << "date_raw,country,treasury_raw,treasury_shadow_raw\n";
            output_.flush();
            AddEventHandler<smedley::events::DailyUpdateEvent>(
                "economy_trace.daily",
                [this](smedley::events::DailyUpdateEvent &event) { OnDailyUpdate(event); });
            logger().Info("writing daily country treasury data to economy_trace.csv");
        }

        void OnUnload() override
        {
            output_.flush();
        }

    private:
        void OnDailyUpdate(smedley::events::DailyUpdateEvent &event)
        {
            const auto *country = event.GetCountry();
            const auto *game_state = smedley::v2::CCurrentGameState::instance();
            if (country == nullptr || game_state == nullptr || !output_) {
                return;
            }
            const int date = game_state->current_date_raw();
            if (date != last_date_) {
                output_.flush();
                last_date_ = date;
            }
            output_ << date << ','
                    << country->tag().str() << ','
                    << country->treasury_raw() << ','
                    << country->treasury_shadow_raw() << '\n';
        }

        std::ofstream output_;
        int last_date_ = (std::numeric_limits<int>::min)();
    };
}

PLUGIN_API smedley::Plugin *CreatePlugin()
{
    return new economy_trace::Plugin();
}
