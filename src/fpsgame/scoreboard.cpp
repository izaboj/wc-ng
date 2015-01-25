// creation of scoreboard
#include "game.h"
using namespace mod::extinfo; //NEW

namespace game
{
    VARP(scoreboard2d, 0, 1, 1);
    VARP(showservinfo, 0, 1, 1);
    VARP(showclientnum, 0, 0, 2); //NEW 1 -> 2
    VARP(showpj, 0, 0, 1);
    VARP(showping, 0, 1, 1);
    VARP(showspectators, 0, 1, 1);
    VARP(highlightscore, 0, 1, 1);
    VARP(showconnecting, 0, 0, 1);

    //NEW
    MODHVARP(scoreboardtextcolorhead, 0, 0xFFFF80, 0xFFFFFF);
    MODHVARP(scoreboardtextcolor, 0, 0xFFFFDD, 0xFFFFFF);
    MODVARP(showextinfo, 0, 1, 1);
    MODVARP(showfrags, 0, 1, 1);
    MODVARP(showdeaths, 0, 1, 1);
    MODVARP(showdamagedealt, 0, 0, 2);
    MODVARP(showkpd, 0, 0, 1);
    MODVARP(showacc, 0, 1, 1);
    MODVARP(showtks, 0, 0, 1);
    MODVARP(showcountry, 0, 3, 5);
    MODVARP(showserveruptime, 0, 0, 1);
    MODVARP(showservermod, 0, 0, 1);
    MODVARP(oldscoreboard, 0, 0, 1);
    MODVARP(showdemotime, 0, 1, 1);
    MODVARP(showspectatorping, 0, 0, 1);
#ifdef ENABLE_IPS
    MODVARP(showip, 0, 0, 1);
    MODHVARP(ipignorecolor, 0, 0xC4C420, 0xFFFFFF);
    MODVARP(showspectatorip, 0, 0, 1);
#else
    int showip = 0;
    int ipignorecolor = 0;
    int showspectatorip = 0;
#endif //ENABLE_IPS
    //NEW END

    static hashset<teaminfo> teaminfos;

    void clearteaminfo()
    {
        teaminfos.clear();
    }

    void setteaminfo(const char *team, int frags)
    {
        teaminfo *t = teaminfos.access(team);
        if(!t) { t = &teaminfos[team]; copystring(t->team, team, sizeof(t->team)); }
        t->frags = frags;
    }

    teaminfo *getteaminfo(const char *team) { return teaminfos.access(team); } //NEW

    static inline bool playersort(const fpsent *a, const fpsent *b)
    {
        if(a->state==CS_SPECTATOR)
        {
            if(b->state==CS_SPECTATOR) return strcmp(a->name, b->name) < 0;
            else return false;
        }
        else if(b->state==CS_SPECTATOR) return true;
        if(m_ctf || m_collect)
        {
            if(a->flags > b->flags) return true;
            if(a->flags < b->flags) return false;
        }
        if(a->frags > b->frags) return true;
        if(a->frags < b->frags) return false;
        return strcmp(a->name, b->name) < 0;
    }

    void getbestplayers(vector<fpsent *> &best)
    {
        loopv(players)
        {
            fpsent *o = players[i];
            if(o->state!=CS_SPECTATOR) best.add(o);
        }
        best.sort(playersort);
        while(best.length() > 1 && best.last()->frags < best[0]->frags) best.drop();
    }

    void getbestteams(vector<const char *> &best)
    {
        if(cmode && cmode->hidefrags())
        {
            vector<teamscore> teamscores;
            cmode->getteamscores(teamscores);
            teamscores.sort(teamscore::compare);
            while(teamscores.length() > 1 && teamscores.last().score < teamscores[0].score) teamscores.drop();
            loopv(teamscores) best.add(teamscores[i].team);
        }
        else
        {
            int bestfrags = INT_MIN;
            enumerates(teaminfos, teaminfo, t, bestfrags = max(bestfrags, t.frags));
            if(bestfrags <= 0) loopv(players)
            {
                fpsent *o = players[i];
                if(o->state!=CS_SPECTATOR && !teaminfos.access(o->team) && best.htfind(o->team) < 0) { bestfrags = 0; best.add(o->team); }
            }
            enumerates(teaminfos, teaminfo, t, if(t.frags >= bestfrags) best.add(t.team));
        }
    }

    struct scoregroup : teamscore
    {
        vector<fpsent *> players;
    };
    static vector<scoregroup *> groups;
    static vector<fpsent *> spectators;

    static inline bool scoregroupcmp(const scoregroup *x, const scoregroup *y)
    {
        if(!x->team)
        {
            if(y->team) return false;
        }
        else if(!y->team) return true;
        if(x->score > y->score) return true;
        if(x->score < y->score) return false;
        if(x->players.length() > y->players.length()) return true;
        if(x->players.length() < y->players.length()) return false;
        return x->team && y->team && strcmp(x->team, y->team) < 0;
    }

    static int groupplayers()
    {
        int numgroups = 0;
        spectators.setsize(0);
        loopv(players)
        {
            fpsent *o = players[i];
            if(!showconnecting && !o->name[0]) continue;
            if(o->state==CS_SPECTATOR) { spectators.add(o); continue; }
            const char *team = m_teammode && o->team[0] ? o->team : NULL;
            bool found = false;
            loopj(numgroups)
            {
                scoregroup &g = *groups[j];
                if(team!=g.team && (!team || !g.team || strcmp(team, g.team))) continue;
                g.players.add(o);
                found = true;
            }
            if(found) continue;
            if(numgroups>=groups.length()) groups.add(new scoregroup);
            scoregroup &g = *groups[numgroups++];
            g.team = team;
            if(!team) g.score = 0;
            else if(cmode && cmode->hidefrags()) g.score = cmode->getteamscore(o->team);
            else { teaminfo *ti = teaminfos.access(team); g.score = ti ? ti->frags : 0; }
            g.players.setsize(0);
            g.players.add(o);
        }
        loopi(numgroups) groups[i]->players.sort(playersort);
        spectators.sort(playersort);
        groups.sort(scoregroupcmp, 0, numgroups);
        return numgroups;
    }

    //NEW
    template<typename T>
    static inline bool displayextinfo(T cond = T(1))
    {
        return cond && showextinfo && ((isconnected(false) && hasextinfo) || (demoplayback && demohasextinfo));
    }

    const char *countryflag(const char *code, bool staticstring = true)
    {
        static hashtable<const char* /* code */, const char* /* filename */> cache;
        bool nullcode = !code;

        if(nullcode)
        {
            code = "UNKNOWN";
            staticstring = true;
        }

        static string buf;
        static mod::strtool s(buf, sizeof(buf));

        const char *filename = cache.find(code, NULL);

        if(filename)
        {
            ret:;
            if(filename == (const char*)-1) return NULL;
            else return filename;
        }

        static const size_t fnoffset = STRLEN("packages/icons/");

        if(!s)
        {
            s.copy("packages/icons/", fnoffset);
            s.fixpathdiv();
        }
        else s -= s.length()-fnoffset;

        s.append(code, nullcode ? STRLEN("UNKNOWN") : 2);
        s.append(".png", 4);

        const char *file = s.str();

#if defined(__GNUC__) && !defined(__clang__) && !defined(__ICC)
        // workaround for a bogus gcc warning
        // gcc thinks "file" is an empty string constant, while it clearly isn't
        // warning: offset outside bounds of constant string
        if(!s) __builtin_unreachable();
#endif

        if(fileexists(findfile(file, "rb"), "rb")) filename = newstring(file+fnoffset);
        else filename = (const char*)-1;

        if(!staticstring) code = newstring(code);

        cache.access(code, filename);
        goto ret;
    }

    ICOMMAND(countryflagpath, "s", (const char *code), const char *fn = countryflag(code, false); result(fn ? fn : ""));

    void rendercountry(g3d_gui &g, const char *code, const char *name, int mode, bool *clicked, int scoreboard)
    {
        const char *dcode = code && *code ? code : "??";
        const char *dname = name && *name ? name : "??";

        int colour;

        switch(scoreboard)
        {
            case 1: colour = scoreboardtextcolor; break;
            case 2: colour = scoreboardtextcolorhead; break;
            default: colour = 0xFFFFDD;
        }

        /*
         * Mode:
         * 1: Country Code
         * 2: Country Name
         * 3: Country Flag | Country Code
         * 4: Country Flag | Country Name
         * 5: Country Flag
         */

        static string cnamebuf;
        static mod::strtool cname(cnamebuf, sizeof(cnamebuf));
        const char *cflag = mode>2 ? countryflag(code) : NULL;

        if(mode<5)
        {
            cname = (mode%2 ? dcode : dname);
            cname += ' ';
        }
        else cname.clear();

        if(clicked) *clicked = !!(g.button(cname.str(), colour, cflag)&G3D_UP);
        else g.text(cname.str(), colour, cflag);
    }

    static inline void rendercountry(g3d_gui &g, fpsent *o, int mode)
    {
        rendercountry(g, o->countrycode, o->country, mode, NULL, 1);
    }

    static inline void renderip(g3d_gui &g, fpsent *o)
    {
        if(o->extinfo) g.textf("%u.%u.%u", scoreboardtextcolor, NULL, o->extinfo->ip.ia[0], o->extinfo->ip.ia[1], o->extinfo->ip.ia[2]);
        else g.text("??", scoreboardtextcolor);
    }
    //NEW END

    void renderscoreboard(g3d_gui &g, bool firstpass)
    {
        bool haveextinfoips = displayextinfo(showcountry) && gamemod::haveextinfoplayerips(gamemod::EXTINFO_IPS_SCOREBOARD); //NEW
        const ENetAddress *address = connectedpeer();
        if(showservinfo && (address || demohasservertitle)) //NEW || demohasservertitle
        {
            //NEW
            if(demohasservertitle)
            {
                if(demoplayback) g.titlef("%.25s", scoreboardtextcolorhead, NULL, servinfo);
            }
            else
            //NEW END
            {
                string hostname;
                if(enet_address_get_host_ip(address, hostname, sizeof(hostname)) >= 0)
                {
                    if(servinfo[0]) g.titlef("%.25s", scoreboardtextcolorhead, NULL, servinfo);
                    else g.titlef("%s:%d", scoreboardtextcolorhead, NULL, hostname, address->port);
                }
            }
        }

        g.pushlist();
        g.spring();
        //NEW
        if(demoplayback && showdemotime && gametimestamp)
        {
            string buf;
            time_t ts = gametimestamp+((lastmillis-mapstart)/1000);
            struct tm *tm = localtime(&ts);
            strftime(buf, sizeof(buf), "%x %X", tm);
            g.text(buf, scoreboardtextcolorhead);
            g.separator();
        }
        //NEW END
        g.text(server::modename(gamemode), scoreboardtextcolorhead);
        g.separator();
        const char *mname = getclientmap();
        g.text(mname[0] ? mname : "[new map]", scoreboardtextcolorhead);
        extern int gamespeed;
        if(!demoplayback && mastermode != MM_OPEN) { g.separator(); g.textf("%s%s\fr", scoreboardtextcolorhead, NULL, mastermodecolor(mastermode, "\f0"), server::mastermodename(mastermode)); } //NEW
        if(gamespeed != 100) { g.separator(); g.textf("%d.%02dx", scoreboardtextcolorhead, NULL, gamespeed/100, gamespeed%100); }
        if(m_timed && mname[0] && (maplimit >= 0 || intermission))
        {
            g.separator();
            if(intermission) g.text("intermission", scoreboardtextcolorhead);
            else
            {
                extern int scaletimeleft; //NEW
                int secs = max(maplimit-lastmillis, 0)/1000*100/(scaletimeleft ? gamespeed : 100), mins = secs/60; //NEW 100/(scaletimeleft ? gamespeed : 100)
                secs %= 60;
                g.pushlist();
                g.strut(mins >= 10 ? 4.5f : 3.5f);
                g.textf("%d:%02d", scoreboardtextcolorhead, NULL, mins, secs);
                g.poplist();
            }
        }
        //NEW
        if(displayextinfo(showserveruptime))
        {
            int uptime = mod::extinfo::getserveruptime();
            if(uptime>0)
            {
                string buf;
                mod::strtool tmp(buf, sizeof(buf));
                tmp.fmtseconds(uptime);
                g.separator();
                g.pushlist();
                g.textf("server uptime: %s", scoreboardtextcolorhead, NULL, tmp.str());
                g.poplist();
            }
        }
        if(displayextinfo(showservermod))
        {
            const char *servermod = mod::extinfo::getservermodname();
            if(servermod)
            {
                g.separator();
                g.pushlist();
                g.textf("server mod: %s", scoreboardtextcolorhead, NULL, servermod);
                g.poplist();
            }
        }
        if(displayextinfo(showcountry))
        {
            uint32_t ip = 0;
            if(curpeer) ip = curpeer->address.host;
            else if(demoplayback && demohasservertitle) ip = demoserver.host;
            if(ip)
            {
                static const char *country;
                static const char *countrycode;
                static uint32_t lastip = 0;
                if(lastip != ip)
                {
                    country = mod::geoip::country(ip);
                    countrycode = mod::geoip::countrycode(ip);
                    lastip = ip;
                }
                if(country && countrycode)
                {
                    g.separator();
                    g.pushlist();
                    rendercountry(g, countrycode, country, showcountry, NULL, 2);
                    g.poplist();
                }
            }
        }
        //NEW END
        if(ispaused()) { g.separator(); g.text("paused", scoreboardtextcolorhead); }
        g.spring();
        g.poplist();

        g.separator();

        int numgroups = groupplayers();
        loopk(numgroups)
        {
            if((k%2)==0) g.pushlist(); // horizontal

            scoregroup &sg = *groups[k];
            int bgcolor = sg.team && m_teammode ? (isteam(player1->team, sg.team) ? 0x3030C0 : 0xC03030) : 0,
                fgcolor = scoreboardtextcolorhead;

            g.pushlist(); // vertical
            g.pushlist(); // horizontal

            #define loopscoregroup(o, b) \
                loopv(sg.players) \
                { \
                    fpsent *o = sg.players[i]; \
                    b; \
                }

            g.pushlist();
            if(sg.team && m_teammode)
            {
                g.pushlist();
                g.background(bgcolor, numgroups>1 ? 3 : 5);
                g.strut(1);
                g.poplist();
            }
            g.text("", 0, " ");
            loopscoregroup(o,
            {
                bool isignored = mod::ipignore::isignored(o->clientnum, NULL); //NEW
                if((o==player1 && highlightscore && (multiplayer(false) || demoplayback || players.length() > 1)) || isignored) //NEW || isignored
                {
                    g.pushlist();
                    g.background(isignored ? ipignorecolor : 0x808080, numgroups>1 ? 3 : 5); //NEW isignored ? ipignorecolor :
                }
                const playermodelinfo &mdl = getplayermodelinfo(o);
                const char *icon = sg.team && m_teammode ? (isteam(player1->team, sg.team) ? mdl.blueicon : mdl.redicon) : mdl.ffaicon;
                g.text("", 0, icon);
                if((o==player1 && highlightscore && (multiplayer(false) || demoplayback || players.length() > 1)) || isignored) g.poplist(); //NEW || isignored
            });
            g.poplist();

            if(sg.team && m_teammode)
            {
                g.pushlist(); // vertical

                if(sg.score>=10000) g.textf("%s: WIN", fgcolor, NULL, sg.team);
                else g.textf("%s: %d", fgcolor, NULL, sg.team, sg.score);

                g.pushlist(); // horizontal
            }

            if(!cmode || !cmode->hidefrags() || displayextinfo(showfrags)) //NEW || displayextinfo(showfrags)
            {
                bool hidefrags = cmode && cmode->hidefrags(); //NEW
                g.pushlist();
                g.strut(6);
                g.text("frags", fgcolor);
                //NEW
                loopscoregroup(o,
                {
                    if(hidefrags && o->flags) g.textf("%d/%d", scoreboardtextcolor, NULL, o->frags, o->flags);
                    else g.textf("%d", scoreboardtextcolor, NULL, o->frags);
                });
                //NEW END
                g.poplist();
            }

            if(oldscoreboard) goto next; //NEW
            name:; //NEW

            g.pushlist();
            g.text("name", fgcolor);
            g.strut(13);
            loopscoregroup(o,
            {
                int status = o->state!=CS_DEAD ? scoreboardtextcolor : 0x606060;
                if(o->privilege)
                {
                    status = gamemod::guiprivcolor(o->privilege);                     //NEW replaced "o->privilege>=PRIV_ADMIN ? 0xFF8000 : 0x40FF80" with "gamemod::guiprivcolor(o->privilege)"
                    if(o->state==CS_DEAD) status = (status>>1)&0x7F7F7F;
                }
                g.textf("%s ", status, NULL, colorname(o));
            });
            g.poplist();

            if(oldscoreboard) goto cn;
            next:; //NEW

            //NEW
            if(displayextinfo<bool>())
            {
                if(showdeaths)
                {
                    g.pushlist();
                    g.strut(6);
                    g.text("deaths", fgcolor);

                    loopscoregroup(o,
                    {
                        if(o->extinfo) g.textf("%d", scoreboardtextcolor, NULL, o->extinfo->deaths);
                        else g.text("??", scoreboardtextcolor);
                    });

                    g.poplist();
                }

                if(showdamagedealt)
                {
                    // More or less the same as
                    // https://github.com/sauerworld/community-edition/blob/2e31507b5d5/fpsgame/scoreboard.cpp#L314

                    g.pushlist();
                    g.strut(6);
                    g.text("dd", fgcolor);

                    loopscoregroup(o,
                    {
                        bool get = o->damagedealt>=1000 || showdamagedealt==2;
                        int damagedealt;
                        if(o->extinfo && (o->extinfo->ext.ishopmodcompatible() || o->extinfo->ext.isoomod())) damagedealt = max(o->damagedealt, o->extinfo->ext.damage);
                        else damagedealt = o->damagedealt;
                        g.textf(get ? "%.2fk" : "%.0f", scoreboardtextcolor, NULL, get ? damagedealt/1000.f : damagedealt*1.f));
                    }

                    g.poplist();
                }

                if(showkpd)
                {
                    g.pushlist();
                    g.strut(6);
                    g.text("kpd", fgcolor);

                    loopscoregroup(o,
                    {
                        if(o->extinfo) g.textf("%.2f", scoreboardtextcolor, NULL, o->extinfo->frags / (o->extinfo->deaths > 0 ? o->extinfo->deaths*1.f : 1.00f));
                        else g.text("??", scoreboardtextcolor);
                    });

                    g.poplist();
                }

                if(showacc)
                {
                    g.pushlist();
                    g.strut(6);
                    g.text("acc", fgcolor);

                    loopscoregroup(o,
                    {
                        if(o->extinfo) g.textf("%d%%", scoreboardtextcolor, NULL, o->extinfo->acc);
                        else g.text("??", scoreboardtextcolor);
                    });

                    g.poplist();
                }

                if(showtks && m_teammode)
                {
                    g.pushlist();
                    g.strut(8);
                    g.text("teamkills", fgcolor);

                    string tks;
                    loopscoregroup(o, g.text(gamemod::calcteamkills(o, tks, sizeof(tks)) ? tks : "??", scoreboardtextcolor, NULL));

                    g.poplist();
                }

                if(showip)
                {
                    g.pushlist();
                    g.strut(11);
                    g.text("ip", fgcolor);
                    loopscoregroup(o, renderip(g, o));
                    g.poplist();
                }
            }
            //NEW END

            if(multiplayer(false) || demoplayback)
            {
                if(showpj)
                {
                    g.pushlist();
                    g.strut(6);
                    g.text("pj", fgcolor);
                    loopscoregroup(o,
                    {
                        if(o->state==CS_LAGGED) g.text("LAG", scoreboardtextcolor);
                        else g.textf("%d", scoreboardtextcolor, NULL, o->plag);
                    });
                    g.poplist();
                }

                if(showping)
                {
                    g.pushlist();
                    g.text("ping", fgcolor);
                    g.strut(6);
                    loopscoregroup(o,
                    {
                        fpsent *p = o->ownernum >= 0 ? getclient(o->ownernum) : o;
                        if(!p) p = o;
                        if(!showpj && p->state==CS_LAGGED) g.text("LAG", scoreboardtextcolor);
                        else g.textf("%d", scoreboardtextcolor, NULL, p->ping);
                    });
                    g.poplist();
                }

                //NEW
                if(displayextinfo(showcountry) && haveextinfoips)
                {
                    g.pushlist();
                    g.text("country", fgcolor);
                    g.strut(8);
                    loopscoregroup(o, rendercountry(g, o, showcountry););
                    g.poplist();
                }
                //NEW END
            }

            if(oldscoreboard) goto name; //NEW
            cn:; //NEW

            if(showclientnum==1 || player1->privilege>=PRIV_MASTER) //NEW showclientnum || --> showclientnum==1 ||
            {
                g.space(1);
                g.pushlist();
                g.text("cn", fgcolor);
                loopscoregroup(o, g.textf("%d", scoreboardtextcolor, NULL, o->clientnum));
                g.poplist();
            }

            if(sg.team && m_teammode)
            {
                g.poplist(); // horizontal
                g.poplist(); // vertical
            }

            g.poplist(); // horizontal
            g.poplist(); // vertical

            if(k+1<numgroups && (k+1)%2) g.space(2);
            else g.poplist(); // horizontal
        }

        if(showspectators && spectators.length())
        {
            if(showclientnum || player1->privilege>=PRIV_MASTER)
            {
                g.pushlist();

                g.pushlist();
                g.text("spectator", scoreboardtextcolorhead, " ");
                loopv(spectators)
                {
                    fpsent *o = spectators[i];
                    int status = scoreboardtextcolor;
                    bool isignored = mod::ipignore::isignored(o->clientnum, NULL);       //NEW
                    if(o->privilege) status = gamemod::guiprivcolor(o->privilege);       //NEW replaced "o->privilege>=PRIV_ADMIN ? 0xFF8000 : 0x40FF80" with "gamemod::guiprivcolor(o->privilege)"
                    if((o==player1 && highlightscore) || isignored)                      //NEW || isignored
                    {
                        g.pushlist();
                        g.background(isignored ? ipignorecolor : 0x808080, 3);           //NEW mod::isignored() ? ipignorecolor :
                    }
                    g.text(colorname(o), status, "spectator");
                    if((o==player1 && highlightscore) || isignored) g.poplist();         //NEW || isignored
                }
                g.poplist();

                if(showclientnum==1) //NEW
                {
                    g.space(1);
                    g.pushlist();
                    g.text("cn", scoreboardtextcolorhead);
                    loopv(spectators) g.textf("%d", scoreboardtextcolor, NULL, spectators[i]->clientnum);
                    g.poplist();
                }

                //NEW
                if(showspectatorping)
                {
                    g.space(1);
                    g.pushlist();
                    g.text("ping", scoreboardtextcolorhead);
                    loopv(spectators) g.textf("%d", scoreboardtextcolor, NULL, spectators[i]->ping);
                    g.poplist();
                }

                if(displayextinfo(showspectatorip))
                {
                    g.space(1);
                    g.pushlist();
                    g.text("ip", scoreboardtextcolorhead);
                    loopv(spectators) renderip(g, spectators[i]);
                    g.poplist();
                }

                if(displayextinfo(showcountry) && haveextinfoips)
                {
                    g.space(1);
                    g.pushlist();
                    g.text("country", scoreboardtextcolorhead);
                    loopv(spectators) rendercountry(g, spectators[i], showcountry);
                    g.poplist();
                }
                //NEW END

                g.poplist();
            }
            else
            {
                g.textf("%d spectator%s", scoreboardtextcolorhead, " ", spectators.length(), spectators.length()!=1 ? "s" : "");
                loopv(spectators)
                {
                    if((i%3)==0)
                    {
                        g.pushlist();
                        g.text("", scoreboardtextcolor, "spectator");
                    }
                    fpsent *o = spectators[i];
                    int status = scoreboardtextcolor;
                    bool isignored = mod::ipignore::isignored(o->clientnum, NULL); //NEW
                    if(o->privilege) status = gamemod::guiprivcolor(o->privilege); //NEW replaced "o->privilege>=PRIV_ADMIN ? 0xFF8000 : 0x40FF80" with "gamemod::guiprivcolor(o->privilege)"
                    if((o==player1 && highlightscore) || isignored)                //NEW || isignored
                    {
                        g.pushlist();
                        g.background(isignored ? ipignorecolor : 0x808080);        //NEW mod::isignored() ? ipignorecolor :
                    }
                    g.text(colorname(o), status, countryflag(o->extinfo ? o->countrycode : NULL)); //NEW countryflag() instead "spectator"
                    if((o==player1 && highlightscore) || isignored) g.poplist();   //NEW || isignored
                    if(i+1<spectators.length() && (i+1)%3) g.space(1);
                    else g.poplist();
                }
            }
        }
    }

    struct scoreboardgui : g3d_callback
    {
        bool showing;
        vec menupos;
        int menustart;

        scoreboardgui() : showing(false) {}

        void show(bool on)
        {
            if(!showing && on)
            {
                menupos = menuinfrontofplayer();
                menustart = starttime();
            }
            showing = on;
        }

        void gui(g3d_gui &g, bool firstpass)
        {
            g.start(menustart, 0.03f, NULL, false);
            renderscoreboard(g, firstpass);
            g.end();
        }

        void render()
        {
            if(showing) g3d_addgui(this, menupos, (scoreboard2d ? GUI_FORCE_2D : GUI_2D | GUI_FOLLOW) | GUI_BOTTOM);
        }

    } scoreboard;

    void g3d_gamemenus()
    {
        scoreboard.render();
    }

    VARFN(scoreboard, showscoreboard, 0, 0, 1, scoreboard.show(showscoreboard!=0));

    bool isscoreboardshown()
    {
        return showscoreboard != 0; //NEW
    }

    void showscores(bool on)
    {
        showscoreboard = on ? 1 : 0;
        scoreboard.show(on);
    }
    ICOMMAND(showscores, "D", (int *down), showscores(*down!=0));
}

