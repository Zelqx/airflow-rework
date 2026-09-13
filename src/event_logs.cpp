#include "globals.hpp"
#include "ryze_paths.hpp"
#include "event_logs.hpp"
#include "Windows.h"
#include <cstring>
#include <fstream>
#include "../legacy ui/menu/menu.h"
#include "../legacy ui/config_system.h"

// Define the trash talk messages arrays at global scope
const char* style0_messages[] = {
    "The only thing lower than your k/d ratio is your I.Q.",
    "Better buy PC, stop playing at school library",
    "Studies show that aiming gives you better chances of hitting your target.",
    "You should let your chair play, at least it knows how to support.",
    "Youre the human equivalent of a participation award.",
    "I'm not trash talking, Im talking to trash",
    "Legend has it that the number 0 was first invented after scientists calculated your chance of doing something useful.",
    "You're the type of player to get 3rd place in a 1v1 match",
    "Does your ass ever get jealous of the amount of shit that comes out of your mouth",
    "I bet your brain feels as good as new, seeing that you never use it.",
    "Some people get paid to suck, you do it for free.",
    "You're as dense as a brick, but honestly a less useful one.",
    "Your aim is so poor that people held a fundraiser for it",
    "The only thing more unreliable than you is the condom your dad used.",
    "Calling you a retard is a compliment in comparison to how stupid you actually are.",
    "I didnt know dying was a special ability.",
    "If I jumped from your ego to your intelligence, Id die of starvation half-way down.",
    "You should turn the game off. Just walk outside and apologize to a tree for wasting oxygen.",
    "You're an inspiration for birth control.",
    "Some babies were dropped on their heads but you were clearly thrown at a wall",
    "Two wrongs dont make a right, take your parents as an example.",
    "You're the reason abortion was legalized",
    "Who set the bots to passive?",
    "I'm surprised that you were able to hit the Install button",
    "Internet Explorer is faster than your reactions.",
    "You're about as useful as pedals on a wheelchair.",
    "You have some big balls on you. Too bad they belong to the guy behind you.",
    "If only you could hit an enemy as much as your dad hits you.",
    "My dead dad has better aim than you, it only took him one bullet",
    "Options -> How To Play",
    "Nice $4750 decoy you just bought.",
    "Even Noah can't carry these animals without skeet.cc.",
    "1 dog",
    "1 nn",
    "1.",
    "1",
    "ez?",
    "EZ",
    "Is it a joke?",
    "Sell your computer and buy a Wii.",
    "Idk if u know but it's mouse1 to shoot.",
    "How did you change your difficulty settings? My CS:GO is stuck on easy",
    "You are the reason why people say the CS:GO community sucks.",
    "I'm okay with this team. I work in the city as a garbage collector.",
    "I'd call you cancer, but at least cancer gets kills"
};

const char* style1_messages[] = {
    "oooh~~ i love to put my dick through masters thigh-highs, what about u faggy~?",
    "you look cute when you try to 1tap me uwu",
    "i'll crush you while wearing my cute stockings~",
    "you're my personal punching bag for my spicy fantasies~",
    "come closer, let's see if you can handle my skill and my cock~",
    "is that all you got, kitten? i need more~",
    "your tears taste better than energy drinks, owo~",
    "lemme see you sweat for me~",
    "you're so weak, I could carry you in my panties~",
    "awww, did you miss again? cutie~",
    "you like getting pwned by a femboy, huh?~",
    "oh my~ that headshot felt sooo good~",
    "can't handle me, can you, little bunny~",
    "your mouse hand looks tiny next to me~",
    "better step up, or I'll tickle you to death~",
    "you smell like defeat~ so cute~",
    "my stockings are wetter than your tears~",
    "did you just ragequit? teehee~",
    "come here~ let me show you how to really spray~",
    "you're my uwu target dummy~",
    "pwease don't cry~ I'll give you hugs~",
    "oh, your skill is so tiny~ owo~",
    "get on your knees, it's practice time~",
    "I'll own you with my cuteness~",
    "you're cute when you fail~",
    "mmm~ your reactions are so slow~",
    "better git gud or I spank you~",
    "you're my little game toy~",
    "I luv pwning you like this~",
    "oooh~ your aim is as soft as my stockings~",
    "let me show you how to shoot properly~",
    "come closer, little fella~",
    "you're my uwu plaything~",
    "oh my~ miss again?~",
    "you're so slow, I could tie bows on you~",
    "this game is so fun with you~",
    "you're a failure but adorable~",
    "come on~ hit me if you can~",
    "can't escape my cute pwns~",
    "you're mine to tease~",
    "let's make this death more fun~",
    "you're trembling~ so kawaii~",
    "I could own you all day~",
    "so weak~ so precious~",
    "I'll pwnt you again~",
    "you're my little humiliation~",
    "miss again? teehee~",
    "you're my uwu target~",
    "lose again~ so cute~",
    "my stockings are victorious~",
    "you can't handle me~",
    "come closer for more~",
    "you're adorable when you fail~"
};

const char* style2_messages[] = {
    "Twój K/D to jedyna rzecz bardziej żałosna niż twoja morda, pierdolony debilu.",
    "Celujesz jak pijany chuj z jaskinią w oku - gówno trafiasz!",
    "Ogarnij sobie sprzęt, a nie grasz na mikrofalówce z przedszkola, kurwa mać!",
    "Jesteś tak pierdolnięty, że twoja głowa służy ci tylko do darcia japy.",
    "Umieranie to twoja jedyna umiejętność - powinieneś dawać tutoriale, ciężarze jebany!",
    "Przestań kurwa strzelać na ślepo, może wreszcie trafisz w coś, niecelny chuju!",
    "Twój gameplay to czyste gówno - nawet twoje krzesło wie więcej o tej grze!",
    "Jesteś żywym dowodem, że prezerwatywa twojego starego powinna była wygrać!",
    "Nie gadam z tobą - obserwuję jak się kompromitujesz, jebany pajacu!",
    "Masz tyle odwagi co królik w rui, a skończysz jako czyjś bitch, pedale!",
    "Twój mózg to jedyny organ, który masz jeszcze dziewiczy - użyj go, kurwa!",
    "Każde twoje słowo potwierdza, że twoi rodzice to jebani kretyni!",
    "Ruchasz się tak beznadziejnie, że nawet darmowe dziwki płakałyby nad tobą!",
    "Z tobą w drużynie czuję się jak przy zbiórce odpadów radioaktywnych!",
    "Jesteś tak gęsty, że twoja czaszka przetrwa wojnę nuklearną - szkoda że pusta!",
    "Kto włączył tryb 'chuj wie o co chodzi'? A, to ty swoim graniem!",
    "Jesteś żywym argumentem za aborcją - szkoda że twoja stara nie miała rozumu!",
    "Tobą to ktoś rzucał jak jebaną piłką do kosza - celując w beton!",
    "Na jaką zbiórkę wpłacić, żebyś wreszcie przestał być jebanym beztalenciem?",
    "Twoja poduszka animowana to jedyna istota z ciebie dumna - bo nie ma mózgu!",
    "Gdyby twoi starcy byli mniej jebnięci, może nie byłbyś takim nieporozumieniem!",
    "Ostatni raz czułeś miłość wciskając się w wiaderko KFC, przegrywie!",
    "Pan Bóg cię pomylił z jebanym workiem treningowym, zero wyjątkowości!",
    "Jakim cudem udało ci się kurwa zainstalować tę grę? To większy cud niż twoje birth!",
    "Masz jaja tylko po to, żeby cię pieprzyli w dupę, bo do gry się nie nadajesz, ciota.",
    "Twój mózg ma więcej kurwa zakazów wstępu niż strzeżony obiekt wojskowy.",
    "Twoi rodzice to powinni iść do piachu za spłodzenie takiej jebanej porażki.",
    "Jesteś jak odpad nuklearny - toksyczny, bezużyteczny i wszyscy chcą cię zamknąć w beczce.",
    "Gęstość twojego mózgu konkuruje z czarną dziurą, tylko że nawet światło ucieka od ciebie.",
    "Jedyna rzecz bardziej zawodna od ciebie to prezerwatywa, której twój stary nie założył!"
};
const char* style3_messages[] = {
"✞ ＲＹＺＥ ✞ rezolv.exe loaded, ego.exe crashed", 
"uwu~ RYZE just deleted ur confidence folder", 
"ＲＹＺＥ > ur paste cried, ur aim died ✧", 
"Error404: ur skill not found, ＲＹＺＥ debugging u rn", 
"ℜ𝔶𝔷𝔢 activated godmode", 
"☯ ＲＹＺＥ channeling hvh monk chi ☯", 
"ＲＹＺＥ just alt+F4’d ur ego", 
"∩(°▽°∩) ＲＹＺＥ rezolv lvl = 9999", 
"ＲＹＺＥ compiling ur tears.cpp...", 
"Your aimbot stuttered when it saw ＲＹＺＥ", 
"░R░Y░Z░E░ rewriting ur death scene ░", 
"ＲＹＺＥ > ur paste is now freeware", 
"✨ ＲＹＺＥ blessed this 1tap ✨", 
"ＲＹＺＥ.exe ran successfully, ur K/D ratio didn’t", 
"RYZE: turning ur PC into emotional support device", 
"☠ skeetless again ☠ ＲＹＺＥ ate ur config ☠", 
"ＲＹＺＥ just turned off ur recoil controller", 
"1 tap = free therapy, courtesy of ＲＹＺＥ", 
"Uploading ur humiliation.mp4 to C: drive...", 
"ＲＹＺＥ detected cope.exe running", 
"ＲＹＺＥ whispers: 'delete system32 for better fps'", 
"ur paste shaking like my aim in warmup", 
"ＲＹＺＥ say ur brain pinged 900ms", 
"[$$$] ＲＹＺＥ driver signed by pure chaos [$$$]", 
"ＲＹＺＥ > hvh monk says u need firmware update", 
"ＲＹＺＥ installing new recoil laws of physics", 
"ur cheat crying in C++, ＲＹＺＥ writing in pain++", 
"✪ ＲＹＺＥ ✪ rezolv more than ur school grades", 
"ＲＹＺＥ just optimized ur L ratio", 
"ur aim died peacefully, ＲＹＺＥ officiated", 
"ＲＹＺＥ compiling new insult module... success!", 
"ＲＹＺＥ patched ur confidence exploit", 
"ＲＹＺＥ v666 | changelog: added ur tears", 
"ＲＹＺＥ detected human.exe crash", 
"ＲＹＺＥ blessing this 1tap in 4K HDR", 
"System message: ＲＹＺＥ detected coping mechanism", 
"ＲＹＺＥ debugging ur soul rn...", 
"ＲＹＺＥ crowned king of hvh server again", 
"ＲＹＺＥ said gg, but ur ego heard gg ez", 
"ＲＹＺＥ rezolv u smoother than butter.dll", 
"[$$$] ＲＹＺＥ loaded, prepare for emotional damage", 
"ＲＹＺＥ logs: ur aim crashed into reality.exe", 
"∞ rezolv ∞ ryze ∞ ur deaths ∞", 
"ＲＹＺＥ.exe compiling sarcasm module...",
"ＲＹＺＥ hvh priest blessing ur config", 
"ＲＹＺＥ: ur monitor watching in shame", 
"✞ bow before rezolv god, ＲＹＺＥ ✞" 
};

const char* style4_messages[] = {
    "codex checked the replay: terminal skill issue",
    "your crosshair filed a missing persons report",
    "patched your peek, shipped your defeat",
    "commit message: removed one opponent from round",
    "your config compiled, your aim did not",
    "codex says: unresolved symbol, expected aim",
    "that peek failed code review",
    "stack trace points directly to your ego",
    "your timing returned false again",
    "refactored you out of the server",
    "build passed, you did not",
    "your resolver guessed harder than your homework",
    "deleted by a one-line change",
    "this round had no merge conflicts until you peeked",
    "codex linted your movement and found only warnings",
    "your spray pattern needs documentation",
    "hotfix deployed: you are spectator now",
    "unit test result: one tap successful",
    "your angle was deprecated",
    "rollback requested, confidence not found",
    "opened an issue for your aim, closed as won't fix",
    "your peek was experimental and it broke production",
    "codex ran the numbers, you were the bug",
    "patched the round with your obituary",
    "clean diff: +1 kill, -1 problem"
};

static std::string truncate_name(const std::string& name, size_t max_len = 20)
{
    if (name.length() <= max_len)
        return name;
    return name.substr(0, max_len) + "...";
}
void c_event_logs::on_item_purchase(c_game_event* event)
{
    if (std::strcmp(event->get_name(), CXOR("item_purchase")) || !HACKS->local)
        return;

    if (!(g_cfg.visuals.eventlog.logs & 8))
        return;

    auto userid = event->get_int(CXOR("userid"));
    if (!userid)
        return;

    auto user_id = HACKS->engine->get_player_for_user_id(userid);
    auto player = (c_cs_player*)HACKS->entity_list->get_client_entity(user_id);
    if (!player)
        return;

    if (player->is_teammate())
        return;

    push_message(tfm::format(CXOR("%s bought %s"), truncate_name(player->get_name()), event->get_string(CXOR("weapon"))));
}

void c_event_logs::on_bomb_plant(c_game_event* event)
{
    const char* event_name = event->get_name();
    if (!(g_cfg.visuals.eventlog.logs & 16))
        return;

    auto userid = event->get_int(CXOR("userid"));
    if (!userid)
        return;

    auto user_id = HACKS->engine->get_player_for_user_id(userid);
    auto player = (c_cs_player*)HACKS->entity_list->get_client_entity(user_id);
    if (!player)
        return;

    if (!std::strcmp(event_name, CXOR("bomb_planted")))
        push_message(tfm::format(CXOR("%s planted the bomb"), truncate_name(player->get_name())));

    if (!std::strcmp(event_name, CXOR("bomb_begindefuse")))
        push_message(tfm::format(CXOR("%s is defusing the bomb"), truncate_name(player->get_name())));
}

void c_event_logs::on_player_hurt(c_game_event* event)
{
    if (!(g_cfg.visuals.eventlog.logs & 1))
        return;

    if (std::strcmp(event->get_name(), CXOR("player_hurt")) || !HACKS->local)
        return;

    auto attacker = HACKS->engine->get_player_for_user_id(event->get_int(CXOR("attacker")));
    if (HACKS->local->index() != attacker)
        return;

    auto user_id = HACKS->engine->get_player_for_user_id(event->get_int(CXOR("userid")));
    auto player = (c_cs_player*)HACKS->entity_list->get_client_entity(user_id);

    if (!player)
        return;

    if (player->is_teammate())
        return;

    auto group = event->get_int(CXOR("hitgroup"));
    auto dmg_health = event->get_int(CXOR("dmg_health"));
    auto health = event->get_int(CXOR("health"));

    auto string_group = main_utils::hitgroup_to_string(group);

    if (group == HITGROUP_GENERIC || group == HITGROUP_GEAR)
        push_message(tfm::format(CXOR("Hit %s for %d (%d left)"),
            truncate_name(player->get_name()),
            dmg_health,
            health));
    else
        push_message(tfm::format(CXOR("Hit %s in %s for %d (%d left)"),
            truncate_name(player->get_name()),
            string_group.c_str(),
            dmg_health,
            health));
}

static std::vector<std::string> load_trash_talk_lines(const std::string& filename)
{
    std::vector<std::string> insults;
    std::ifstream file(ryze_paths::trash_talk_folder() + "\\" + filename);
    if (!file.is_open())
        return insults;

    std::string line;
    while (std::getline(file, line))
    {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        size_t start = 0, end;
        while ((end = line.find(',', start)) != std::string::npos)
        {
            std::string insult = line.substr(start, end - start);
            insult.erase(0, insult.find_first_not_of(" \t\r\n"));
            insult.erase(insult.find_last_not_of(" \t\r\n") + 1);
            if (!insult.empty())
                insults.push_back(insult);
            start = end + 1;
        }
        std::string last = line.substr(start);
        last.erase(0, last.find_first_not_of(" \t\r\n"));
        last.erase(last.find_last_not_of(" \t\r\n") + 1);
        if (!last.empty())
            insults.push_back(last);
    }
    return insults;
}

void c_event_logs::on_player_death(c_game_event* event)
{
    if (!(g_cfg.visuals.eventlog.logs & 2))
        return;

    if (std::strcmp(event->get_name(), CXOR("player_death")) || !HACKS->local)
        return;

    auto attacker_userid = event->get_int(CXOR("attacker"));
    if (!attacker_userid)
        return;

    auto attacker_index = HACKS->engine->get_player_for_user_id(attacker_userid);
    if (attacker_index != HACKS->engine->get_local_player())
        return;

    auto victim_userid = event->get_int(CXOR("userid"));
    if (!victim_userid)
        return;

    auto victim_index = HACKS->engine->get_player_for_user_id(victim_userid);
    auto victim = (c_cs_player*)HACKS->entity_list->get_client_entity(victim_index);
    if (!victim)
        return;

    if (victim->is_teammate())
        return;

    auto weapon = event->get_string(CXOR("weapon"));
    push_message(tfm::format(CXOR("Killed %s with %s"), truncate_name(victim->get_name()), weapon));

    if (g_cfg.visuals.eventlog.kill_say_enable)
    {
        // safe execute wrapper: validate HACKS and sanitize the message to avoid
        // passing binary/newline content into engine->execute_client_cmd
        auto safe_say = [&](const std::string& msg)
            {
                if (!HACKS || !HACKS->engine)
                    return;

                std::string out = msg;
                // remove newlines to avoid breaking the console command
                out.erase(std::remove_if(out.begin(), out.end(), [](char c) { return c == '\n' || c == '\r'; }), out.end());
                // clamp to reasonable length
                if (out.size() > 190)
                    out.resize(190);

                std::string cmd = std::string("say ") + out;
                HACKS->engine->execute_client_cmd(cmd.c_str(), nullptr);
            };

        std::vector<std::string> file_lines;
        const char** list = nullptr;
        int count = 0;

        switch (g_cfg.visuals.eventlog.kill_say_style)
        {
        case 0:
            list = style0_messages;
            count = sizeof(style0_messages) / sizeof(style0_messages[0]);
            break;
        case 1:
            list = style1_messages;
            count = sizeof(style1_messages) / sizeof(style1_messages[0]);
            break;
        case 2:
            list = style2_messages;
            count = sizeof(style2_messages) / sizeof(style2_messages[0]);
            break;
        case 3:
            list = style3_messages;
            count = sizeof(style3_messages) / sizeof(style3_messages[0]);
            break;
        case 4:
            file_lines = load_trash_talk_lines("codex.txt");
            if (file_lines.empty())
            {
                list = style4_messages;
                count = sizeof(style4_messages) / sizeof(style4_messages[0]);
            }
            break;
        default:
            break;
        }

        if (!file_lines.empty())
        {
            auto& msg = file_lines[rand() % file_lines.size()];
            safe_say(msg);
        }
        else if (list && count > 0)
        {
            const char* msg = list[rand() % count];
            if (msg)
                safe_say(msg);
        }
    }
}

void c_event_logs::on_game_events(c_game_event* event)
{
    const char* event_name = event->get_name();

    if (!std::strcmp(event_name, CXOR("player_death")))
        on_player_death(event);
    else if (!std::strcmp(event_name, CXOR("player_hurt")))
        on_player_hurt(event);
    else if (!std::strcmp(event_name, CXOR("bomb_planted")) || !std::strcmp(event_name, CXOR("bomb_begindefuse")))
        on_bomb_plant(event);
    else if (!std::strcmp(event_name, CXOR("item_purchase")))
        on_item_purchase(event);
}

void c_event_logs::filter_console()
{
    HACKS->convars.con_filter_text->fn_change_callbacks.remove_count();
    HACKS->convars.con_filter_enable->fn_change_callbacks.remove_count();

    if (set_console)
    {
        set_console = false;

        HACKS->cvar->find_convar(CXOR("developer"))->set_value(0);
        HACKS->convars.con_filter_enable->set_value(1);
        HACKS->convars.con_filter_text->set_value(CXOR(""));
    }

    auto filter = g_cfg.visuals.eventlog.enable && g_cfg.visuals.eventlog.filter_console;
    if (log_value != filter)
    {
        log_value = filter;

        if (!log_value)
            HACKS->convars.con_filter_text->set_value(CXOR(""));
        else
            HACKS->convars.con_filter_text->set_value(CXOR("IrWL5106TZZKNFPz4P4Gl3pSN?J370f5hi373ZjPg%VOVh6lN"));
    }
}

void c_event_logs::render_logs()
{
    if (!g_cfg.visuals.eventlog.enable)
        return;

    constexpr auto font_size = 15.f;
    auto render_font = RENDER->fonts.eventlog;
    auto time = HACKS->system_time();

    float y = 8.f;
    for (int i = 0; i < event_logs.size(); ++i)
    {
        auto event_log = &event_logs[i];

        auto time_left = 1.f - std::clamp((time - event_log->life_time) / 5.f, 0.f, 1.f);
        if (time_left <= 0.5f)
        {
            float f = std::clamp(time_left, 0.0f, .5f) / .5f;

            event_log->clr.a() = ((std::uint8_t)(f * 255.0f));

            if (i == 0 && f < 0.2f)
                y -= font_size * (1.0f - f / 0.2f);

            if (time_left <= 0.f)
            {
                event_logs.erase(event_logs.begin() + i);
                continue;
            }
        }

        RENDER->text(10.f, y, event_log->clr, FONT_DROPSHADOW, &render_font, event_log->message);
        y += font_size;
    }
}
