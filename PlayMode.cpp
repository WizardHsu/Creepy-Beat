#include "PlayMode.hpp"

#include "LitColorTextureProgram.hpp"

#include "DrawLines.hpp"
#include "Mesh.hpp"
#include "Load.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"

#include <cstddef>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

#include <random>

GLuint phonebank_meshes_for_lit_color_texture_program = 0;
Load< MeshBuffer > phonebank_meshes(LoadTagDefault, []() -> MeshBuffer const * {
	MeshBuffer const *ret = new MeshBuffer(data_path("phone-bank.pnct"));
	phonebank_meshes_for_lit_color_texture_program = ret->make_vao_for_program(lit_color_texture_program->program);
	return ret;
});

Load< Scene > phonebank_scene(LoadTagDefault, []() -> Scene const * {
	return new Scene(data_path("phone-bank.scene"), [&](Scene &scene, Scene::Transform *transform, std::string const &mesh_name){
		Mesh const &mesh = phonebank_meshes->lookup(mesh_name);

		scene.drawables.emplace_back(transform);
		Scene::Drawable &drawable = scene.drawables.back();

		drawable.pipeline = lit_color_texture_program_pipeline;

		drawable.pipeline.vao = phonebank_meshes_for_lit_color_texture_program;
		drawable.pipeline.type = mesh.type;
		drawable.pipeline.start = mesh.start;
		drawable.pipeline.count = mesh.count;

	});
});

WalkMesh const *walkmesh = nullptr;
Load< WalkMeshes > phonebank_walkmeshes(LoadTagDefault, []() -> WalkMeshes const * {
	WalkMeshes *ret = new WalkMeshes(data_path("phone-bank.w"));
	walkmesh = &ret->lookup("WalkMesh");
	return ret;
});

Load< Sound::Sample > beat(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("./beat.opus"));
});

PlayMode::PlayMode() : scene(*phonebank_scene) {
	//create a player transform:
	scene.transforms.emplace_back();
	player.transform = &scene.transforms.back();

	//create a player camera attached to a child of the player transform:
	scene.transforms.emplace_back();
	scene.cameras.emplace_back(&scene.transforms.back());
	player.camera = &scene.cameras.back();
	player.camera->fovy = glm::radians(60.0f);
	player.camera->near = 0.01f;
	player.camera->transform->parent = player.transform;

	//player's eyes are 1.8 units above the ground:
	player.camera->transform->position = glm::vec3(0.0f, 0.0f, 1.8f);

	//rotate camera facing direction (-z) to player facing direction (+y):
	player.camera->transform->rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));

	//start player walking at nearest walk point:
	player.at = walkmesh->nearest_walk_point(player.transform->position);

	//get the ghosts transform
	for (auto &transform : scene.transforms)
	{
		std::string objName = transform.name.substr(0, transform.name.find("_"));
		if (objName == "ghost")
		{
			uint32_t index = static_cast<uint32_t>(std::atoi(&transform.name[transform.name.size() - 1]));
			ghosts[index] = &transform;		
			ghosts_show[index] = true;
		}
		else if (objName == "Spindle")
		{
			startPlatform = &transform;	
		}
		else if (objName == "CrossSword")
		{
			crossSword = &transform;	
		}

	}

	originalScale = ghosts[0]->scale;
	current_reset_pos = player.transform->position;

}

PlayMode::~PlayMode() {
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {

	if (evt.type == SDL_KEYDOWN) {
		if (evt.key.keysym.sym == SDLK_ESCAPE) {
			SDL_SetRelativeMouseMode(SDL_FALSE);
			return true;
		} else if (evt.key.keysym.sym == SDLK_a) {
			left.downs += 1;
			left.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_d) {
			right.downs += 1;
			right.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_w) {
			up.downs += 1;
			up.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_s) {
			down.downs += 1;
			down.pressed = true;
			return true;
		}
	} else if (evt.type == SDL_KEYUP) {
		if (evt.key.keysym.sym == SDLK_a) {
			left.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_d) {
			right.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_w) {
			up.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_s) {
			down.pressed = false;
			return true;
		}
	} else if (evt.type == SDL_MOUSEBUTTONDOWN) {
		if (SDL_GetRelativeMouseMode() == SDL_FALSE) {
			SDL_SetRelativeMouseMode(SDL_TRUE);
			return true;
		}
	} else if (evt.type == SDL_MOUSEMOTION) {
		if (SDL_GetRelativeMouseMode() == SDL_TRUE) {
			glm::vec2 motion = glm::vec2(
				evt.motion.xrel / float(window_size.y),
				-evt.motion.yrel / float(window_size.y)
			);
			glm::vec3 up = walkmesh->to_world_smooth_normal(player.at);
			player.transform->rotation = glm::angleAxis(-motion.x * player.camera->fovy, up) * player.transform->rotation;

			float pitch = glm::pitch(player.camera->transform->rotation);
			pitch += motion.y * player.camera->fovy;
			//camera looks down -z (basically at the player's feet) when pitch is at zero.
			pitch = std::min(pitch, 0.95f * 3.1415926f);
			pitch = std::max(pitch, 0.05f * 3.1415926f);
			player.camera->transform->rotation = glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f));

			return true;
		}
	}

	return false;
}

float dist(Scene::Transform *a, Scene::Transform *b){
	float xd = a->position.x - b->position.x; 
	float yd = a->position.y - b->position.y;
	float zd = a->position.z - b->position.z;
	float dist = std::sqrt(xd * xd + yd * yd + zd * zd);
	return dist;
}

void PlayMode::update(float elapsed) {

	//check game is end
	if (isGameEnd) return;

	//timer
	current_time += elapsed;

	//check palyer state
	if (isPlayerDie) 
	{
		player.transform->position = current_reset_pos;
		player.at = walkmesh->nearest_walk_point(player.transform->position);
		isPlayerDie = false;
		return;
	}

	//game scene change
	if (soundPlayingProgress == 0)
	{
		playingSound = Sound::play(*beat, 0.8f, 0.0f);
		soundPlayingProgress = 1;
	}

	current_beat_period -= elapsed;

	if (current_beat_period <= unit_period && !isWaiting)
	{
		isWaiting = true;
		for (size_t i = 1; i < ghosts.size(); ++i)
		{
			if (i % 3 == current_state) continue;
			ghosts[i]->scale = glm::vec3(0.0f, 0.0f, 0.0f);
			ghosts_show[i] = false;
		}
		current_state++;
		if (current_state == 3) current_state = 0;
	}
	else if (current_beat_period <= 0)
	{
		for (size_t i = 1; i < ghosts.size(); ++i)
		{
			ghosts[i]->scale = originalScale;
			ghosts_show[i] = true;
		}
		isWaiting = false;
		current_beat_period = beat_period;
		soundPlayingProgress ++;
		
		//reset sound
		if (soundPlayingProgress == 3)
		{
			playingSound->stop();
			soundPlayingProgress = 0;
		}
	}

	//player walking:
	{
		//combine inputs into a move:
		constexpr float PlayerSpeed = 3.0f;
		glm::vec2 move = glm::vec2(0.0f);
		if (left.pressed && !right.pressed) move.x =-1.0f;
		if (!left.pressed && right.pressed) move.x = 1.0f;
		if (down.pressed && !up.pressed) move.y =-1.0f;
		if (!down.pressed && up.pressed) move.y = 1.0f;

		//make it so that moving diagonally doesn't go faster:
		if (move != glm::vec2(0.0f)) move = glm::normalize(move) * PlayerSpeed * elapsed;

		//get move in world coordinate system:
		glm::vec3 remain = player.transform->make_local_to_world() * glm::vec4(move.x, move.y, 0.0f, 0.0f);

		//using a for() instead of a while() here so that if walkpoint gets stuck in
		// some awkward case, code will not infinite loop:
		for (uint32_t iter = 0; iter < 10; ++iter) {
			if (remain == glm::vec3(0.0f)) break;
			WalkPoint end;
			float time;
			walkmesh->walk_in_triangle(player.at, remain, &end, &time);
			player.at = end;
			if (time == 1.0f) {
				//finished within triangle:
				remain = glm::vec3(0.0f);
				break;
			}
			//some step remains:
			remain *= (1.0f - time);
			//try to step over edge:
			glm::quat rotation;
			if (walkmesh->cross_edge(player.at, &end, &rotation)) {
				//stepped to a new triangle:
				player.at = end;
				//rotate step to follow surface:
				remain = rotation * remain;
			} else {
				//ran into a wall, bounce / slide along it:
				glm::vec3 const &a = walkmesh->vertices[player.at.indices.x];
				glm::vec3 const &b = walkmesh->vertices[player.at.indices.y];
				glm::vec3 const &c = walkmesh->vertices[player.at.indices.z];
				glm::vec3 along = glm::normalize(b-a);
				glm::vec3 normal = glm::normalize(glm::cross(b-a, c-a));
				glm::vec3 in = glm::cross(normal, along);

				//check how much 'remain' is pointing out of the triangle:
				float d = glm::dot(remain, in);
				if (d < 0.0f) {
					//bounce off of the wall:
					remain += (-1.25f * d) * in;
				} else {
					//if it's just pointing along the edge, bend slightly away from wall:
					remain += 0.01f * d * in;
				}
			}
		}

		if (remain != glm::vec3(0.0f)) {
			std::cout << "NOTE: code used full iteration budget for walking." << std::endl;
		}

		//update player's position to respect walking:
		player.transform->position = walkmesh->to_world_point(player.at);

		{ //update player's rotation to respect local (smooth) up-vector:
			
			glm::quat adjust = glm::rotation(
				player.transform->rotation * glm::vec3(0.0f, 0.0f, 1.0f), //current up vector
				walkmesh->to_world_smooth_normal(player.at) //smoothed up vector at walk location
			);
			player.transform->rotation = glm::normalize(adjust * player.transform->rotation);
		}

		/*
		glm::mat4x3 frame = camera->transform->make_local_to_parent();
		glm::vec3 right = frame[0];
		//glm::vec3 up = frame[1];
		glm::vec3 forward = -frame[2];

		camera->transform->position += move.x * right + move.y * forward;
		*/
	}


	// period -= elapsed;
	// if (period < 0)
	// {
	// 	std::cout << "ghost 1: " << dist(player.transform,  ghosts[1]) << std::endl;
	// 	std::cout << "ghost 2: " << dist(player.transform,  ghosts[2]) << std::endl;
	// 	period = 2.0f;
	// }

	//reset button press counters:
	left.downs = 0;
	right.downs = 0;
	up.downs = 0;
	down.downs = 0;

	//check if get the cross sword or come back
	if (dist(player.transform, crossSword) < 1.0f && !isArriveCross)
	{
		crossSword->scale = glm::vec3(0.0f, 0.0f, 0.0f);
		current_reset_pos = crossSword->position;
		isArriveCross = true;
	}
	else if (dist(player.transform, startPlatform) < 1.0f && isArriveCross)
	{
		isGameEnd = true;
		playingSound->stop();
		return;
	}


	//check player collide with showing up platform
	float temp_dist = 3.0f;
	size_t location = 0;
	for (size_t i = 1; i < ghosts.size(); ++i)
	{
		float current_dist = dist(player.transform, ghosts[i]);
		if (current_dist > temp_dist) continue;
		temp_dist = current_dist;
		location = i;
	}
	if (!ghosts_show[location])
	{
		isPlayerDie = true;
	}
}


std::string getTimeString (float current_time) {
	uint8_t minute =  static_cast<uint8_t>(current_time / 60.0f);
	std::string minute_string = std::to_string(minute);
	if (minute < 10) minute_string = "0" + minute_string;
	uint8_t second = static_cast<uint8_t>(current_time - minute * 60);
	std::string second_string = std::to_string(second);
	if (second < 10) second_string = "0" + second_string;
	return minute_string + ":" + second_string;
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	//update camera aspect ratio for drawable:
	player.camera->aspect = float(drawable_size.x) / float(drawable_size.y);

	//set up light type and position for lit_color_texture_program:
	// TODO: consider using the Light(s) in the scene to do this
	glUseProgram(lit_color_texture_program->program);
	glUniform1i(lit_color_texture_program->LIGHT_TYPE_int, 1);
	glUniform3fv(lit_color_texture_program->LIGHT_DIRECTION_vec3, 1, glm::value_ptr(glm::vec3(0.0f, 0.0f,-1.0f)));
	glUniform3fv(lit_color_texture_program->LIGHT_ENERGY_vec3, 1, glm::value_ptr(glm::vec3(1.0f, 1.0f, 0.95f)));
	glUseProgram(0);

	glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
	glClearDepth(1.0f); //1.0 is actually the default value to clear the depth buffer to, but FYI you can change it.
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS); //this is the default depth comparison function, but FYI you can change it.

	scene.draw(*player.camera);

	{ //use DrawLines to overlay some text:
		glDisable(GL_DEPTH_TEST);
		float aspect = float(drawable_size.x) / float(drawable_size.y);
		DrawLines lines(glm::mat4(
			1.0f / aspect, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		));

		std::string infor = isGameEnd ? "GOAL!" : "Mouse motion looks; WASD moves; escape ungrabs mouse";
		constexpr float H = 0.09f;
		lines.draw_text(infor,
			glm::vec3(-aspect + 0.1f * H, -1.0 + 0.1f * H, 0.0),
			glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
			glm::u8vec4(0x00, 0x00, 0x00, 0x00));
		float ofs = 2.0f / drawable_size.y;
		lines.draw_text(infor,
			glm::vec3(-aspect + 0.1f * H + ofs, -1.0 + + 0.1f * H + ofs, 0.0),
			glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
			glm::u8vec4(0xff, 0xff, 0xff, 0x00));

		std::string counter_string = getTimeString(current_time);
		constexpr float h = 0.2f;
		lines.draw_text(counter_string,
			glm::vec3(-aspect + 0.1f * h, -1.0 + 8.5f * h, 0.0),
			glm::vec3(h, 0.0f, 0.0f), glm::vec3(0.0f, h, 0.0f),
			glm::u8vec4(0x00, 0x00, 0x00, 0x00));
		lines.draw_text(counter_string,
			glm::vec3(-aspect + 0.1f * h + ofs, -1.0 + 8.5f * h + ofs, 0.0),
			glm::vec3(h, 0.0f, 0.0f), glm::vec3(0.0f, h, 0.0f),
			glm::u8vec4(0xff, 0xff, 0xff, 0x00));

	}
	GL_ERRORS();
}

