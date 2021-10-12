#include "Mode.hpp"

#include "Scene.hpp"
#include "WalkMesh.hpp"
#include "Sound.hpp"


#include <cstddef>
#include <glm/glm.hpp>

#include <vector>
#include <deque>
#include <array>

struct PlayMode : Mode {
	PlayMode();
	virtual ~PlayMode();

	//functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//----- game state -----

	//input tracking:
	struct Button {
		uint8_t downs = 0;
		uint8_t pressed = 0;
	} left, right, down, up;

	//local copy of the game scene (so code can change it during gameplay):
	Scene scene;

	//player info:
	struct Player {
		WalkPoint at;
		//transform is at player's feet and will be yawed by mouse left/right motion:
		Scene::Transform *transform = nullptr;
		//camera is at player's head and will be pitched by mouse up/down motion:
		Scene::Camera *camera = nullptr;
	} player;

	//ghost
	std::array<Scene::Transform *, 10> ghosts;
	std::array<bool, 10> ghosts_show;
	glm::vec3 originalScale;

	//platform
	Scene::Transform * startPlatform;
	Scene::Transform * crossSword;
	
	//game state
	float beat_period = 3.25f;
	float unit_period = beat_period / 4.0f;
	float current_beat_period = beat_period;
	size_t current_state = 1;
	bool isWaiting = false;
	size_t soundPlayingProgress = 0;
	std::shared_ptr< Sound::PlayingSample > playingSound;
	bool isPlayerDie = false;
	glm::vec3 current_reset_pos;
	float current_time = 0.0f;
	bool isArriveCross = false;
	bool isGameEnd = false;

};
