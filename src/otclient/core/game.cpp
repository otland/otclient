/*
 * Copyright (c) 2010-2012 OTClient <https://github.com/edubart/otclient>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "game.h"
#include "localplayer.h"
#include "map.h"
#include "tile.h"
#include <otclient/net/protocolgame.h>
#include <framework/core/eventdispatcher.h>
#include <framework/ui/uimanager.h>

Game g_game;

void Game::loginWorld(const std::string& account, const std::string& password, const std::string& worldHost, int worldPort, const std::string& characterName)
{
    m_online = false;
    m_dead = false;
    m_protocolGame = ProtocolGamePtr(new ProtocolGame);
    m_protocolGame->login(account, password, worldHost, (uint16)worldPort, characterName);
}

void Game::cancelLogin()
{
    processLogout();
}

void Game::logout(bool force)
{
    if(!m_protocolGame || !m_online)
        return;

    m_protocolGame->sendLogout();

    if(force)
        processLogout();
}

void Game::processLoginError(const std::string& error)
{
    g_lua.callGlobalField("Game", "onLoginError", error);
}

void Game::processConnectionError(const boost::system::error_code& error)
{
    // connection errors only have meaning if we still have a protocol
    if(m_protocolGame) {
        if(error != asio::error::eof)
            g_lua.callGlobalField("Game", "onConnectionError", error.message());

        processLogout();
    }
}

void Game::processLogin(const LocalPlayerPtr& localPlayer)
{
    m_localPlayer = localPlayer;
    m_online = true;
    g_lua.callGlobalField("Game", "onLogin", m_localPlayer);
}

void Game::processLogout()
{
    if(m_online) {
        g_lua.callGlobalField("Game", "onLogout", m_localPlayer);

        m_localPlayer.reset();
        m_online = false;
    }

    if(m_protocolGame) {
        m_protocolGame->disconnect();
        m_protocolGame.reset();
    }
}

void Game::processDeath()
{
    m_dead = true;
    g_lua.callGlobalField("Game","onDeath");

    // force logout in five seconds
    g_dispatcher.scheduleEvent(std::bind(&Game::forceLogout, &g_game), 5 * 1000);
}

void Game::processTextMessage(int type, const std::string& message)
{
    g_lua.callGlobalField("Game","onTextMessage", type, message);
}

void Game::processInventoryChange(int slot, const ItemPtr& item)
{
    if(item)
        item->setPos(Position(65535, slot, 0));

    g_lua.callGlobalField("Game","onInventoryChange", slot, item);
}

void Game::processAttackCancel()
{
    if(m_localPlayer->isAttacking())
        m_localPlayer->setAttackingCreature(nullptr);
}

void Game::walk(Otc::Direction direction)
{
    if(m_localPlayer->isFollowing()) {
        cancelFollow();
        return;
    }

    if(!isOnline() || isDead() || !checkBotProtection() || !m_localPlayer->canWalk(direction))
        return;

    m_localPlayer->clientWalk(direction);

    switch(direction) {
    case Otc::North:
        m_protocolGame->sendWalkNorth();
        break;
    case Otc::East:
        m_protocolGame->sendWalkEast();
        break;
    case Otc::South:
        m_protocolGame->sendWalkSouth();
        break;
    case Otc::West:
        m_protocolGame->sendWalkWest();
        break;
    case Otc::NorthEast:
        m_protocolGame->sendWalkNorthEast();
        break;
    case Otc::SouthEast:
        m_protocolGame->sendWalkSouthEast();
        break;
    case Otc::SouthWest:
        m_protocolGame->sendWalkSouthWest();
        break;
    case Otc::NorthWest:
        m_protocolGame->sendWalkNorthWest();
        break;
    }
}

void Game::turn(Otc::Direction direction)
{
    if(!m_online)
        return;

    switch(direction) {
    case Otc::North:
        m_protocolGame->sendTurnNorth();
        break;
    case Otc::East:
        m_protocolGame->sendTurnEast();
        break;
    case Otc::South:
        m_protocolGame->sendTurnSouth();
        break;
    case Otc::West:
        m_protocolGame->sendTurnWest();
        break;
    }
}

void Game::look(const ThingPtr& thing)
{
    if(!m_online || !thing || !checkBotProtection())
        return;

    int stackpos = getThingStackpos(thing);
    if(stackpos != -1)
        m_protocolGame->sendLookAt(thing->getPos(), thing->getId(), stackpos);
}

void Game::use(const ThingPtr& thing)
{
    if(!m_online || !thing || !checkBotProtection())
        return;

    int stackpos = getThingStackpos(thing);
    if(stackpos != -1)
        m_protocolGame->sendUseItem(thing->getPos(), thing->getId(), stackpos, 0);// last 0 has something to do with container
}

void Game::attack(const CreaturePtr& creature)
{
    if(!m_online || !creature || !checkBotProtection())
        return;

    if(m_localPlayer->isFollowing())
        cancelFollow();

    m_localPlayer->setAttackingCreature(creature);
    m_protocolGame->sendAttack(creature->getId());
}

void Game::cancelAttack()
{
    m_localPlayer->setAttackingCreature(nullptr);
    m_protocolGame->sendAttack(0);
}

void Game::follow(const CreaturePtr& creature)
{
    if(!m_online || !creature || !checkBotProtection())
        return;

    if(m_localPlayer->isAttacking())
        cancelAttack();

    m_localPlayer->setFollowingCreature(creature);
    m_protocolGame->sendFollow(creature->getId());
}

void Game::cancelFollow()
{
    m_localPlayer->setFollowingCreature(nullptr);
    m_protocolGame->sendFollow(0);
}

void Game::rotate(const ThingPtr& thing)
{
    if(!m_online || !thing || !checkBotProtection())
        return;

    int stackpos = getThingStackpos(thing);
    if(stackpos != -1)
        m_protocolGame->sendRotateItem(thing->getPos(), thing->getId(), stackpos);
}

int Game::getThingStackpos(const ThingPtr& thing)
{
    // thing is at map
    if(thing->getPos().x != 65535) {
        TilePtr tile = g_map.getTile(thing->getPos());
        return tile->getThingStackpos(thing);
    }

    // thing is at container or inventory
    return 0;
}

void Game::talk(const std::string& message)
{
    talkChannel(1, 0, message);
}

void Game::talkChannel(int channelType, int channelId, const std::string& message)
{
    if(!m_online || !checkBotProtection())
        return;

    m_protocolGame->sendTalk(channelType, channelId, "", message);
}

void Game::talkPrivate(int channelType, const std::string& receiver, const std::string& message)
{
    if(!m_online || !checkBotProtection())
        return;

    m_protocolGame->sendTalk(channelType, 0, receiver, message);
}

void Game::inviteToParty(int creatureId)
{
    if(!m_online || !checkBotProtection())
        return;

    m_protocolGame->sendInviteToParty(creatureId);
}

void Game::openOutfitWindow()
{
    if(!m_online || !checkBotProtection())
        return;

    m_protocolGame->sendGetOutfit();
}

void Game::setOutfit(const Outfit& outfit)
{
    if(!m_online || !checkBotProtection())
        return;

    m_protocolGame->sendSetOutfit(outfit);
}

void Game::addVip(const std::string& name)
{
    if(!m_online || name.empty() || !checkBotProtection())
        return;

    m_protocolGame->sendAddVip(name);
}

void Game::removeVip(int playerId)
{
    if(!m_online || !checkBotProtection())
        return;

    m_protocolGame->sendRemoveVip(playerId);
}

bool Game::checkBotProtection()
{
#ifndef DISABLE_BOT_PROTECTION
    if(g_lua.isInCppCallback() && !g_ui.isOnInputEvent()) {
        logError("caught a lua call to a bot protected game function, the call was canceled");
        return false;
    }
#endif
    return true;
}
