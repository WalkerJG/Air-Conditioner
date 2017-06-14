
//
// Air Conditioner - Server MVC Service
// BOT Man, 2017
//

#ifndef AC_SERVER_SERVICE_H
#define AC_SERVER_SERVICE_H

#include <exception>
#include <queue>

#define MAXCLIENT 2
#define THRESHOLD 1.0
#define DEADTIME 3
#define DBNAME "ac.db"

#include "ormlite/ormlite.h"
#include "server-model.h"

#include <iostream>

namespace Air_Conditioner
{
    class LogManager
    {
    public:
        static void TurnOn (const RoomId &room)
        {
            // TODO: impl turn on
        }
        static void TurnOff (const RoomId &room,
                             const TimePoint &time)
        {
            // TODO: impl turn off
        }

        static void BegRequest (const RoomId &room,
                                const ClientState &state)
        {
            // TODO: impl beg req
        }
        static void EndRequest (const RoomId &room)
        {
            // TODO: impl end req
        }

        static std::pair<TimePoint, TimePoint> GetTimeRange ()
        {
            // TODO: impl here
            return std::make_pair (std::chrono::system_clock::now (),
                                   std::chrono::system_clock::now () + std::chrono::hours { 24 });
        }
        static LogOnOffList GetOnOff (const TimePoint &from, const TimePoint &to)
        {
            // TODO: impl here
            LogOnOffList ret;
            ret["john"].emplace_back (LogOnOff {
                std::chrono::system_clock::now (),
                std::chrono::system_clock::now () + std::chrono::hours { 1 }
            });
            ret["lee"];
            return ret;
        }
        static LogRequestList GetRequest (const TimePoint &from, const TimePoint &to)
        {
            // TODO: impl here
            return LogRequestList {};
        }
    };

    class GuestManager
    {
        struct GuestEntity
        {
            RoomId room;
            GuestId guest;
            ORMAP ("Guest", room, guest);
        };

        // In Database
        static BOT_ORM::ORMapper &_mapper ()
        {
            static BOT_ORM::ORMapper mapper (DBNAME);
            static auto hasInit = false;

            if (!hasInit)
            {
                try { mapper.CreateTbl (GuestEntity {}); }
                catch (...) {}
                hasInit = true;
            }
            return mapper;
        }

    public:
        static void AddGuest (const GuestInfo &guest)
        {
            auto &mapper = _mapper ();
            try
            {
                mapper.Insert (GuestEntity { guest.room, guest.guest });
            }
            catch (...)
            {
                throw std::runtime_error (
                    "The room has already been registered");
            }
        }
        static void RemoveGuest (const RoomId &room)
        {
            auto &mapper = _mapper ();
            mapper.Delete (GuestEntity { room, GuestId {} });
        }

        static void AuthGuest (const GuestInfo &guest)
        {
            static GuestEntity entity;
            static auto field = BOT_ORM::FieldExtractor { entity };

            auto &mapper = _mapper ();
            auto count = mapper.Query (entity)
                .Where (
                    field (entity.room) == guest.room &&
                    field (entity.guest) == guest.guest)
                .Aggregate (
                    BOT_ORM::Expression::Count ());

            if (count == nullptr || count == size_t { 0 })
                throw std::runtime_error ("Invalid Room ID or Guest ID");
        }

        static std::list<GuestInfo> GetGuestList ()
        {
            static GuestEntity entity;
            static auto field = BOT_ORM::FieldExtractor { entity };

            auto &mapper = _mapper ();
            auto list = mapper.Query (entity).ToList ();

            std::list<GuestInfo> ret;
            for (auto &entry : list)
            {
                ret.emplace_back (GuestInfo {
                    std::move (entry.room),
                    std::move (entry.guest) });
            }
            return ret;
        }
    };

    class ConfigManager
    {
        // In Memory
        static ServerInfo &_config ()
        {
            static ServerInfo config;
            return config;
        }

    public:
        static void SetConfig (const ServerInfo &config)
        {
            _config () = config;
        }
        static const ServerInfo &GetConfig ()
        {
            return _config ();
        }
    };

    class ScheduleManager
    {
        static ClientList &_clients ()
        {
            static ClientList clients;
            return clients;
        }

        static bool HasWind (const ClientState &state,
                             const ServerInfo &config)
        {
            // Case: Server Off
            if (!config.isOn) return false;

            // Case: Enough already
            if (config.mode == 0 &&
                state.current <= state.target) return false;
            if (config.mode == 1 &&
                state.current >= state.target) return false;

            // Case: Need to send wind
            if (config.mode == 0 &&
                state.current - state.target >= THRESHOLD) return true;
            if (config.mode == 1 &&
                state.target - state.current >= THRESHOLD) return true;

            // Case: Keep the state
            return state.hasWind;
        }

        static void Schedule ()
        {
            auto &clients = _clients ();
            const auto &config = ConfigManager::GetConfig ();

            auto count = 0;
            std::unordered_map<RoomId, bool> hasWindList;
            for (auto &client : clients)
                if (HasWind (client.second, config) && count < 3)
                {
                    hasWindList[client.first] = true;
                    ++count;
                }
                else
                    hasWindList[client.first] = false;

            for (auto &client : clients)
                client.second.hasWind = hasWindList[client.first];
        }

        static void CheckAlive ()
        {
            auto now = std::chrono::system_clock::now ();
            auto deadTime = now - std::chrono::seconds { DEADTIME };

            auto &clients = _clients ();
            for (auto p = clients.begin (); p != clients.end ();)
                if (p->second.pulse < deadTime)
                {
                    auto room = p->first;
                    p = clients.erase (p);

                    // Write to log
                    LogManager::TurnOff (room, now + std::chrono::seconds { 1 });
                    LogManager::EndRequest (room);
                }
                else ++p;
        }

    public:
        static void AddClient (const GuestInfo &room)
        {
            auto &clients = _clients ();

            // Login already
            if (clients.find (room.room) != clients.end ())
                throw std::runtime_error ("Login already");

            // New Login
            clients.emplace (room.room, ClientState {
                room.guest, Temperature { 0 }, Temperature { 0 },
                Wind { 0 }, Energy { 0 }, Cost { 0 }, false,
                std::chrono::system_clock::now ()
            });

            // Write to log
            LogManager::TurnOn (room.room);
        }

        static void Pulse (const RoomRequest &req)
        {
            // Check Alive
            CheckAlive ();

            auto &roomState = GetClient (req.room);
            auto isChanged =
                roomState.current != req.current ||
                roomState.target != req.target ||
                roomState.wind != req.wind;

            roomState.current = req.current;
            roomState.target = req.target;
            roomState.wind = req.wind;

            // Write to log
            if (isChanged)
                LogManager::BegRequest (req.room, roomState);

            // Schedule
            Schedule ();

            // Get Delta Time and Pulse
            auto now = std::chrono::system_clock::now ();
            std::chrono::duration<double> deltaTime = now - roomState.pulse;
            roomState.pulse = now;

            // Calc Energy and Cost
            if (roomState.hasWind)
            {
                // Get Delta Energy
                auto deltaEnergy = Energy { deltaTime.count () / 60.0 };
                if (roomState.wind == 1)
                    deltaEnergy = deltaEnergy * 0.8;
                else if (roomState.wind == 3)
                    deltaEnergy = deltaEnergy * 1.3;

                // Add up energy
                roomState.energy += deltaEnergy;
                roomState.cost = roomState.energy * 5;
            }
        }

        static ClientState &GetClient (const RoomId &room)
        {
            try { return _clients ().at (room); }
            catch (...) { throw std::runtime_error ("No such active room, try login again"); }
        }

        static const ClientList &GetClientList ()
        {
            // Check Alive
            CheckAlive ();

            return _clients ();
        }
    };
}

#endif AC_SERVER_SERVICE_H