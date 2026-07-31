local scheduled = false

function on_daily(event)
    if scheduled then
        return
    end

    scheduled = true
    smedley.after_days(30, function(due_event)
        if smedley.request_pause() then
            smedley.log("queued pause at raw date " .. due_event.date_raw)
        end
    end)
end
