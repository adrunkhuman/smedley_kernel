local last_date

function on_load(context)
    smedley.log("country_log loaded with scripting API " .. context.api_version)
end

function on_daily(event)
    if event.country.tag ~= "ENG" or event.date_raw == last_date then
        return
    end

    last_date = event.date_raw
    smedley.log(string.format(
        "ENG date=%d treasury=%.2f countries=%d",
        event.date_raw,
        event.country.treasury,
        event.world.country_slot_count))
end
