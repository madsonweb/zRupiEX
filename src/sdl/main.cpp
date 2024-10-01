#include <cstdio>
#include <cstdlib>
#include <fstream>

#include <SDL.h>

#include <common/bswp.h>
#include <core/config.h>
#include <core/system.h>
#include <input/input.h>
#include <sound/sound.h>
#include <video/video.h>

#include <SimpleIni.h>
#include <filesystem>

const std::string JC = 
#ifdef _WIN32
    "\\";
#else
    "/";
#endif

namespace SDL
{

using Video::DISPLAY_HEIGHT;
using Video::DISPLAY_WIDTH;

struct Screen
{
	SDL_Renderer* renderer;
	SDL_Window* window;
	SDL_Texture* texture;
};

static Screen screen;

void toggle_fullscreen() {
    SDL_SetWindowFullscreen(screen.window, (SDL_GetWindowFlags(screen.window) & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
}

void initialize(bool fullscreen)
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        printf("Failed to initialize SDL2: %s\n", SDL_GetError());

        exit(0);
    }

    //Try synchronizing drawing to VBLANK
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");

    //Set up SDL screen
    SDL_CreateWindowAndRenderer(2 * DISPLAY_WIDTH, 2 * DISPLAY_HEIGHT, 0, &screen.window, &screen.renderer);
    SDL_SetWindowTitle(screen.window, "Rupi");
    SDL_SetWindowSize(screen.window, 2 * DISPLAY_WIDTH, 2 * DISPLAY_HEIGHT);
    SDL_SetWindowResizable(screen.window, SDL_TRUE);
    SDL_RenderSetLogicalSize(screen.renderer, 2 * DISPLAY_WIDTH, 2 * DISPLAY_HEIGHT);
    if (fullscreen) {SDL_SetWindowFullscreen(screen.window, SDL_WINDOW_FULLSCREEN_DESKTOP);}

    screen.texture = SDL_CreateTexture(screen.renderer, SDL_PIXELFORMAT_ARGB1555, SDL_TEXTUREACCESS_STREAMING, DISPLAY_WIDTH, DISPLAY_HEIGHT);
}

void shutdown() {
    //Destroy window, then kill SDL2
    SDL_DestroyTexture(screen.texture);
    SDL_DestroyRenderer(screen.renderer);
    SDL_DestroyWindow(screen.window);

    SDL_Quit();
}

void update(uint16_t* display_output)
{
    // Draw screen
    SDL_UpdateTexture(screen.texture, NULL, display_output, sizeof(uint16_t) * DISPLAY_WIDTH);
    SDL_RenderCopy(screen.renderer, screen.texture, NULL, NULL);
    SDL_RenderPresent(screen.renderer);
}

}

std::string remove_extension(std::string file_path)
{
    auto pos = file_path.find_last_of(".");
    if (pos == std::string::npos)
    {
        return file_path;
    }

    return file_path.substr(0, pos);
}
std::string get_fname(std::string file_path) {return file_path.substr(file_path.find_last_of("/\\") + 1);}

int main(int argc, char** argv)
{
    const std::string exep = std::filesystem::weakly_canonical(std::filesystem::path(argv[0])).parent_path().string() + JC;
    std::string cnff = exep + "config.ini";
    std::string savdir = exep + "saves";

    if (!std::filesystem::exists(savdir)) {std::filesystem::create_directory(savdir);}

    std::ifstream config_chk(cnff);
    if (!config_chk.good()) {
        config_chk.close();
        std::ofstream config_w(cnff);
        config_w << "[display]\nfullscreen=false\n\n[keybinds]\nSTART=TAB\nA=SPACE\nB=c\nC=f\nD=v\nL1=q\nR1=e\nLEFT=a\nRIGHT=d\nUP=w\nDOWN=s\nFullscreen=F11\n";
        config_w.close();
    } else {config_chk.close();}

    if (argc < 3)
    {
        //Sound ROM currently optional
        printf("Args: <game ROM> <BIOS> [sound BIOS]\n");
        return 1;
    }

    CSimpleIniA config_ini;
    SI_Error irc = config_ini.LoadFile(cnff.c_str());
    if (irc != SI_OK) {
        printf("Failed to load config.ini, please delete it and restart to reset settings or fix it yourself.\n");
        return 1;
    }

    SDL::initialize(config_ini.GetBoolValue("display","fullscreen",false));

    std::string cart_name = argv[1];
    std::string bios_name = argv[2];

    Config::SystemInfo config = {};

    std::ifstream cart_file(cart_name, std::ios::binary);
    if (!cart_file.is_open())
    {
        printf("Failed to open %s\n", cart_name.c_str());
        return 1;
    }

    bool le = cart_file.get() != 0x0E;
    cart_file.seekg(0,std::ios::beg);
    if (!le) {config.cart.rom.assign(std::istreambuf_iterator<char>(cart_file),{});}
    else {
        printf("Found little-endian ROM\n");
        cart_file.seekg(0,std::ios::end);
        std::streampos size = cart_file.tellg();
        cart_file.seekg(0,std::ios::beg);
        for (int i = 0; i < size / 2; i++) {
            uint16_t t16;
            cart_file.read(reinterpret_cast<char*>(&t16),2);
            t16 = Common::bswp16(t16);
            config.cart.rom.push_back(t16 & 0xFF);
            config.cart.rom.push_back(t16 >> 8);
        }
    }
    cart_file.close();

    std::ifstream bios_file(bios_name, std::ios::binary);
    if (!bios_file.is_open())
    {
        printf("Failed to open %s\n", bios_name.c_str());
        return 1;
    }

    config.bios_rom.assign(std::istreambuf_iterator<char>(bios_file), {});
    bios_file.close();

    // If last argument is given, load the sound ROM
    if (argc >= 4)
    {
        std::string sound_rom_name = argv[3];
        std::ifstream sound_rom_file(sound_rom_name, std::ios::binary);
        if (!sound_rom_file.is_open())
        {
            printf("Failed to open %s\n", sound_rom_name.c_str());
            return 1;
        }

        config.sound_rom.assign(std::istreambuf_iterator<char>(sound_rom_file), {});
        sound_rom_file.close();
    }

    //Determine the size of SRAM from the cartridge header
    uint32_t sram_start, sram_end;
    memcpy(&sram_start, config.cart.rom.data() + 0x10, 4);
    memcpy(&sram_end, config.cart.rom.data() + 0x14, 4);
    uint32_t sram_size = Common::bswp32(sram_end) - Common::bswp32(sram_start) + 1;

    //Attempt to load SRAM from a file
    config.cart.sram_file_path = savdir + JC + get_fname(remove_extension(cart_name)) + ".sav";
    printf(("SRAM path: " + config.cart.sram_file_path + "\n").c_str());
    std::ifstream sram_file(config.cart.sram_file_path, std::ios::binary);
    if (!sram_file.is_open())
    {
        printf("Warning: SRAM not found\n");
    }
    else
    {
        printf("Successfully found SRAM\n");
        config.cart.sram.assign(std::istreambuf_iterator<char>(sram_file), {});
        sram_file.close();
    }

    //Ensure SRAM is at the proper size. If no file is loaded, it will be filled with 0xFF.
    //If a file was loaded but was smaller than the SRAM size, the uninitialized bytes will be 0xFF.
    //If the file was larger, then the vector size is clamped
    config.cart.sram.resize(sram_size, 0xFF);

    //Initialize the emulator and all of its subprojects
    System::initialize(config);

    //All subprojects have been initialized, so it is safe to reference them now
    Input::add_key_binding(SDL_GetKeyFromName(config_ini.GetValue("keybinds","START","RETURN")), Input::PAD_START);

    Input::add_key_binding(SDL_GetKeyFromName(config_ini.GetValue("keybinds","A","z")), Input::PAD_A);
    Input::add_key_binding(SDL_GetKeyFromName(config_ini.GetValue("keybinds","B","x")), Input::PAD_B);
    Input::add_key_binding(SDL_GetKeyFromName(config_ini.GetValue("keybinds","C","a")), Input::PAD_C);
    Input::add_key_binding(SDL_GetKeyFromName(config_ini.GetValue("keybinds","D","s")), Input::PAD_D);

    Input::add_key_binding(SDL_GetKeyFromName(config_ini.GetValue("keybinds","L1","q")), Input::PAD_L1);
    Input::add_key_binding(SDL_GetKeyFromName(config_ini.GetValue("keybinds","R1","w")), Input::PAD_R1);

    Input::add_key_binding(SDL_GetKeyFromName(config_ini.GetValue("keybinds","LEFT","LEFT")), Input::PAD_LEFT);
    Input::add_key_binding(SDL_GetKeyFromName(config_ini.GetValue("keybinds","RIGHT","RIGHT")), Input::PAD_RIGHT);
    Input::add_key_binding(SDL_GetKeyFromName(config_ini.GetValue("keybinds","UP","UP")), Input::PAD_UP);
    Input::add_key_binding(SDL_GetKeyFromName(config_ini.GetValue("keybinds","DOWN","DOWN")), Input::PAD_DOWN);

    SDL_Keycode full_key = SDL_GetKeyFromName(config_ini.GetValue("keybinds","Fullscreen","F11"));

    bool has_quit = false;
    while (!has_quit)
    {
        System::run();
        SDL::update(System::get_display_output());

        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            switch (e.type)
            {
            case SDL_QUIT:
                has_quit = true;
                break;
            case SDL_KEYDOWN:
                if (e.key.keysym.sym == SDLK_ESCAPE) {
                    has_quit = true;
                } else if (e.key.keysym.sym == full_key) {
                    SDL::toggle_fullscreen();
                } else {
                    Input::set_key_state(e.key.keysym.sym, true);
                }
                break;
            case SDL_KEYUP:
                if (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == full_key) {
                } else {
                    Input::set_key_state(e.key.keysym.sym, false);
                }
                break;
            case SDL_WINDOWEVENT:
                // Everything slows down when minimized so mute sound
                switch (e.window.event)
                {
                case SDL_WINDOWEVENT_HIDDEN:
                case SDL_WINDOWEVENT_MINIMIZED:
                    Sound::set_mute(true);
                    break;
                case SDL_WINDOWEVENT_SHOWN:
                case SDL_WINDOWEVENT_MAXIMIZED:
                case SDL_WINDOWEVENT_RESTORED:
                    Sound::set_mute(false);
                    break;
                }
            }
        }
    }
    
    System::shutdown();
    SDL::shutdown();

    return 0;
}
