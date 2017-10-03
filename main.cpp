#include "load_save_png.hpp"
#include "GL.hpp"
#include "Meshes.hpp"
#include "Scene.hpp"
#include "read_chunk.hpp"

#include <SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <fstream>

static GLuint compile_shader(GLenum type, std::string const &source);
static GLuint link_program(GLuint vertex_shader, GLuint fragment_shader);

int main(int argc, char **argv) {
	//Configuration:
	struct {
		std::string title = "Game2: Scene";
		glm::uvec2 size = glm::uvec2(800, 600);
	} config;

	//------------  initialization ------------

	//Initialize SDL library:
	SDL_Init(SDL_INIT_VIDEO);

	//Ask for an OpenGL context version 3.3, core profile, enable debug:
	SDL_GL_ResetAttributes();
	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);

	//create window:
	SDL_Window *window = SDL_CreateWindow(
		config.title.c_str(),
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		config.size.x, config.size.y,
		SDL_WINDOW_OPENGL /*| SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI*/
	);

	if (!window) {
		std::cerr << "Error creating SDL window: " << SDL_GetError() << std::endl;
		return 1;
	}

	//Create OpenGL context:
	SDL_GLContext context = SDL_GL_CreateContext(window);

	if (!context) {
		SDL_DestroyWindow(window);
		std::cerr << "Error creating OpenGL context: " << SDL_GetError() << std::endl;
		return 1;
	}

	#ifdef _WIN32
	//On windows, load OpenGL extensions:
	if (!init_gl_shims()) {
		std::cerr << "ERROR: failed to initialize shims." << std::endl;
		return 1;
	}
	#endif

	//Set VSYNC + Late Swap (prevents crazy FPS):
	if (SDL_GL_SetSwapInterval(-1) != 0) {
		std::cerr << "NOTE: couldn't set vsync + late swap tearing (" << SDL_GetError() << ")." << std::endl;
		if (SDL_GL_SetSwapInterval(1) != 0) {
			std::cerr << "NOTE: couldn't set vsync (" << SDL_GetError() << ")." << std::endl;
		}
	}

	//Hide mouse cursor (note: showing can be useful for debugging):
	SDL_ShowCursor(SDL_DISABLE);

	//------------ opengl objects / game assets ------------

	//shader program:
	GLuint program = 0;
	GLuint program_Position = 0;
	GLuint program_Normal = 0;
	GLuint program_mvp = 0;
	GLuint program_itmv = 0;
	GLuint program_to_light = 0;
	{ //compile shader program:
		GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER,
			"#version 330\n"
			"uniform mat4 mvp;\n"
			"uniform mat3 itmv;\n"
			"in vec4 Position;\n"
			"in vec3 Normal;\n"
			"out vec3 normal;\n"
			"void main() {\n"
			"	gl_Position = mvp * Position;\n"
			"	normal = itmv * Normal;\n"
			"}\n"
		);

		GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER,
			"#version 330\n"
			"uniform vec3 to_light;\n"
			"in vec3 normal;\n"
			"out vec4 fragColor;\n"
			"void main() {\n"
			"	float light = max(0.0, dot(normalize(normal), to_light));\n"
			"	fragColor = vec4(light * vec3(1.0, 1.0, 1.0), 1.0);\n"
			"}\n"
		);

		program = link_program(fragment_shader, vertex_shader);

		//look up attribute locations:
		program_Position = glGetAttribLocation(program, "Position");
		if (program_Position == -1U) throw std::runtime_error("no attribute named Position");
		program_Normal = glGetAttribLocation(program, "Normal");
		if (program_Normal == -1U) throw std::runtime_error("no attribute named Normal");

		//look up uniform locations:
		program_mvp = glGetUniformLocation(program, "mvp");
		if (program_mvp == -1U) throw std::runtime_error("no uniform named mvp");
		program_itmv = glGetUniformLocation(program, "itmv");
		if (program_itmv == -1U) throw std::runtime_error("no uniform named itmv");

		program_to_light = glGetUniformLocation(program, "to_light");
		if (program_to_light == -1U) throw std::runtime_error("no uniform named to_light");
	}

	//------------ meshes ------------

	Meshes meshes;

	{ //add meshes to database:
		Meshes::Attributes attributes;
		attributes.Position = program_Position;
		attributes.Normal = program_Normal;

		meshes.load("meshes.blob", attributes);
	}
	
	//------------ scene ------------

	Scene scene;
	//set up camera parameters based on window:
	scene.camera.fovy = glm::radians(60.0f);
	scene.camera.aspect = float(config.size.x) / float(config.size.y);
	scene.camera.near = 0.01f;
	//(transform will be handled in the update function below)

	//add some objects from the mesh library:
	auto add_object = [&](std::string const &name, glm::vec3 const &position, glm::quat const &rotation, glm::vec3 const &scale) -> Scene::Object & {
		Mesh const &mesh = meshes.get(name);
		scene.objects.emplace_back();
		Scene::Object &object = scene.objects.back();
		object.transform.position = position;
		object.transform.rotation = rotation;
		object.transform.scale = scale;
		object.vao = mesh.vao;
		object.start = mesh.start;
		object.count = mesh.count;
		object.program = program;
		object.program_mvp = program_mvp;
		object.program_itmv = program_itmv;
		return object;
	};

	{ //read objects to add from "scene.blob":
		std::ifstream file("scene.blob", std::ios::binary);

		std::vector< char > strings;
		//read strings chunk:
		read_chunk(file, "str0", &strings);

		{ //read scene chunk, add meshes to scene:
			struct SceneEntry {
				uint32_t name_begin, name_end;
				glm::vec3 position;
				glm::quat rotation;
				glm::vec3 scale;
			};
			static_assert(sizeof(SceneEntry) == 48, "Scene entry should be packed");

			std::vector< SceneEntry > data;
			read_chunk(file, "scn0", &data);

			for (auto const &entry : data) {
				if (!(entry.name_begin <= entry.name_end && entry.name_end <= strings.size())) {
					throw std::runtime_error("index entry has out-of-range name begin/end");
				}
				std::string name(&strings[0] + entry.name_begin, &strings[0] + entry.name_end);
				add_object(name, entry.position, entry.rotation, entry.scale);
			}
		}
	}

	//create players and ball:
  Scene::Object *player1 = &add_object("Cube", glm::vec3(0.0f, 3.0f, 0.6f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(0.6f));
  Scene::Object *player2 = &add_object("Cube.001", glm::vec3(0.0f, -6.0f, 0.6f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(0.6f));
  Scene::Object *ball = &add_object("Sphere", glm::vec3(0.0f, -1.7f, 5.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(0.4f));

  glm::vec3 player1_velocity = glm::vec3(0.0f, 0.0f, 0.0f);
  glm::vec3 player2_velocity = glm::vec3(0.0f, 0.0f, 0.0f);
  glm::vec3 ball_velocity = glm::vec3(0.0f, 5.0f, 0.0f);

  //create camera
	struct {
		float radius = 15.0f;
		float elevation = -6.0f;
		float azimuth = 3.12f;
		glm::vec3 target = glm::vec3(0.0f, -2.0f, 0.0f);
	} camera;
			
  scene.camera.transform.position = camera.radius * glm::vec3(
	std::cos(camera.elevation) * std::cos(camera.azimuth),
	std::cos(camera.elevation) * std::sin(camera.azimuth),
	std::sin(camera.elevation)) + camera.target;

	glm::vec3 out = -glm::normalize(camera.target - scene.camera.transform.position);
	glm::vec3 up = glm::vec3(0.0f, 0.0f, 1.0f);
	up = glm::normalize(up - glm::dot(up, out) * out);
	glm::vec3 right = glm::cross(up, out);
	
	scene.camera.transform.rotation = glm::quat_cast(
		glm::mat3(right, up, out)
	);
	scene.camera.transform.scale = glm::vec3(1.0f, 1.0f, 1.0f);

	//------------ game loop ------------

	bool should_quit = false;
  bool game_over = false;
  bool new_level = true;

  float ball_gravity = -3.0f;
  float player_gravity = -2.5f;

  bool player1_left = false;
  bool player1_right = false;
  bool player1_jump = false;

  bool player2_left = false;
  bool player2_right = false;
  bool player2_jump = false;

  bool player1_getting_point = false;
  int num_bounces = 0;
  int player1_score = 0;
  int player2_score = 0;
  
	while (true) {
		static SDL_Event evt;
		while (SDL_PollEvent(&evt) == 1) {
			//handle input:
			if (evt.type == SDL_KEYDOWN && evt.key.keysym.sym == SDLK_ESCAPE) {
				should_quit = true;
			} else if (evt.type == SDL_QUIT) {
				should_quit = true;
				break;
			} else if (evt.type == SDL_MOUSEBUTTONDOWN) {
        if (game_over) {
          should_quit = true;
        } else {
          new_level = false;
        }
      } else if (evt.type == SDL_KEYDOWN || evt.type == SDL_KEYUP) {
        if (!new_level) {
          if (evt.key.keysym.sym == SDLK_w) {
            if (!player1_jump && evt.key.state == SDL_PRESSED) {
              player1_jump = true;
              player1_velocity.z = 2.5f;
            }
          } else if (evt.key.keysym.sym == SDLK_a) {
            player1_left = (evt.key.state == SDL_PRESSED);
            if (player1_left) {
              player1_right = false;
            }
          } else if (evt.key.keysym.sym == SDLK_d) {
            player1_right = (evt.key.state == SDL_PRESSED);
            if (player1_right) {
              player1_left = false;
            }
          } else if (evt.key.keysym.sym == SDLK_UP) {
            if (!player2_jump && evt.key.state == SDL_PRESSED) {
              player2_jump = true;
              player2_velocity.z = 2.5f;
            }
          } else if (evt.key.keysym.sym == SDLK_LEFT) {
            player2_left = (evt.key.state == SDL_PRESSED);
            if (player2_right) {
              player2_left = false;
            }
          } else if (evt.key.keysym.sym == SDLK_RIGHT) {
            player2_right = (evt.key.state == SDL_PRESSED);
            if (player2_right) {
              player2_left = false;
            }
          }
        }
      }
		}
		if (should_quit) break;

		auto current_time = std::chrono::high_resolution_clock::now();
		static auto previous_time = current_time;
		float elapsed = std::chrono::duration< float >(current_time - previous_time).count();
    (void) elapsed;
		previous_time = current_time;

		//update game state:
    if (!new_level) {

			//player1
      if (player1->transform.position.z > 0.6f) {
        player1_velocity.z += player_gravity * elapsed;
      } else {
        if (player1_velocity.z < 0.0f) {
          player1_velocity.z = 0.0f;
          player1_jump = false;
        }
      }
     
      if (player1_left) {
        player1_velocity.y = 4.0f;
      } else if (player1_right) {
        player1_velocity.y = -4.0f;
      } else {
        player1_velocity.y = 0.0f;
      }

      player1->transform.position.y += player1_velocity.y * elapsed;
      player1->transform.position.z += player1_velocity.z * elapsed;
      if (player1->transform.position.y <= -0.4f) {
        player1->transform.position.y = -0.4f;
      }
      if (player1->transform.position.y >= 7.7f) {
        player1->transform.position.y = 7.7f;
      }
      
			//player2
      if (player2->transform.position.z > 0.6f) {
        player2_velocity.z += player_gravity * elapsed;
      } else {
        if (player2_velocity.z < 0.0f) {
          player2_velocity.z = 0.0f;
          player2_jump = false;
        }
      }
      
      if (player2_left) {
        player2_velocity.y = 4.0f;
      } else if (player2_right) {
        player2_velocity.y = -4.0f;
      } else {
        player2_velocity.y = 0.0f;
      }
      
      player2->transform.position.y += player2_velocity.y * elapsed;
      player2->transform.position.z += player2_velocity.z * elapsed;
      if (player2->transform.position.y >= -3.0f) {
        player2->transform.position.y = -3.0f;
      }
      if (player2->transform.position.y <= -11.0f) {
        player2->transform.position.y = -11.0f;
      }
      
			//ball
      if (ball->transform.position.z >= 5.0f) {
        ball_velocity.z = 0.0f;
        ball->transform.position.z = 5.0f;
      }  
      if (ball->transform.position.z >= 0.4f) {
        ball_velocity.z += ball_gravity * elapsed;
      } else {
        num_bounces++;
        ball_velocity.z *= -1.0f;
        ball->transform.position.z = 0.4f;
      }
     
			//ball-wall collisions
      if (ball->transform.position.y >= 8.1f) {
        ball_velocity.y *= -1.0f;
        ball->transform.position.y = 8.1f;
      }    
      if (ball->transform.position.y <= -11.4f) {
        ball_velocity.y *= -1.0f;
        ball->transform.position.y = -11.4f;
      }
      
      //ball-player collisions
      glm::vec3 center_difference1 = glm::vec3(
        0.0f,
        ball->transform.position.y - player1->transform.position.y,
        ball->transform.position.z - player1->transform.position.z);
      glm::vec3 center_difference2 = glm::vec3(
        0.0f,
        ball->transform.position.y - player2->transform.position.y,
        ball->transform.position.z - player2->transform.position.z);

      if (-1.0f < center_difference1.y && center_difference1.y < 1.0f &&
          -1.0f < center_difference1.z && center_difference1.z < 1.0f) {
        num_bounces++;
        if (center_difference1.y <= 0.0f &&
            center_difference1.z <= -center_difference1.y &&
            center_difference1.z >= center_difference1.y) {
          //right collision
          ball_velocity.y *= -1.0f;
          ball->transform.position.y = player1->transform.position.y - 1.0f;
        } else if (center_difference1.y > 0.0f &&
            center_difference1.z <= center_difference1.y &&
            center_difference1.z >= -center_difference1.y) {
          //left collision
          ball_velocity.y *= -1.0f;
          ball->transform.position.y = player1->transform.position.y + 1.0f;
        } else if (center_difference1.z <= 0.0f &&
            center_difference1.y <= -center_difference1.z &&
            center_difference1.y >= center_difference1.z) {
          //bottom collision
          ball_velocity.z *= -1.0f;
          ball->transform.position.z = player1->transform.position.z - 1.0f;
        } else if (center_difference1.z > 0.0f &&
            center_difference1.y <= center_difference1.z &&
            center_difference1.y >= -center_difference1.z) {
          //top collision
          ball_velocity.z *= -1.0f;
          ball->transform.position.z = player1->transform.position.z + 1.0f;
        }
      }
        
      if (-1.0f < center_difference2.y && center_difference2.y < 1.0f &&
          -1.0f < center_difference2.z && center_difference2.z < 1.0f) {
        num_bounces++;
        if (center_difference2.y <= 0.0f &&
            center_difference2.z <= -center_difference2.y &&
            center_difference2.z >= center_difference2.y) {
          //right collision
          ball_velocity.y *= -1.0f;
          ball->transform.position.y = player2->transform.position.y - 1.0f;
        } else if (center_difference2.y > 0.0f &&
            center_difference2.z <= center_difference2.y &&
            center_difference2.z >= -center_difference2.y) {
          //left collision
          ball_velocity.y *= -1.0f;
          ball->transform.position.y = player2->transform.position.y + 1.0f;
        } else if (center_difference2.z <= 0.0f &&
            center_difference2.y <= -center_difference2.z &&
            center_difference2.y >= center_difference2.z) {
          //bottom collision
          ball_velocity.z *= -1.0f;
          ball->transform.position.z = player2->transform.position.z - 1.0f;
        } else if (center_difference2.z > 0.0f &&
            center_difference2.y <= center_difference2.z &&
            center_difference2.y >= -center_difference2.z) {
          //top collision
          ball_velocity.z *= -1.0f;
          ball->transform.position.z = player2->transform.position.z + 1.0f;
        }
      }

      //ball-net collisions
      if (ball->transform.position.z <= 3.2f &&
          ball->transform.position.y <= -1.3f &&
          ball->transform.position.y >= -2.1f) {
        num_bounces = 5;
      }
      
      if (num_bounces >= 5) {
        new_level = true;
        num_bounces = 0;
        ball->transform.position = glm::vec3(0.0f, -1.7f, 5.0f);
        player1->transform.position = glm::vec3(0.0f, 3.0f, 0.6f);
        player2->transform.position = glm::vec3(0.0f, -6.0f, 0.6f);
        player1_velocity = glm::vec3(0.0f, 0.0f, 0.0f);
        player2_velocity = glm::vec3(0.0f, 0.0f, 0.0f);

        player1_left = false;
        player1_right = false;
        player1_jump = false;

        player2_left = false;
        player2_right = false;
        player2_jump = false;

        if (player1_getting_point) {
          player1_score++;
          ball_velocity = glm::vec3(0.0f, -5.0f, 0.0f);
          add_object("Sphere", glm::vec3(0.0f, 8.0 - ((float)player1_score) * 0.5f, 7.5f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(0.1f));
          if (player1_score == 10) {
            game_over = true;
            player1->transform.position.z = 5.0f;
          }

        } else {
          player2_score++;
          ball_velocity = glm::vec3(0.0f, 5.0f, 0.0f);
          add_object("Sphere", glm::vec3(0.0f, -12.3 + ((float)player2_score) * 0.5f, 7.5f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(0.1f));
          if (player2_score == 10) {
            game_over = true;
            player2->transform.position.z = 5.0f;
          }
        }

      } else {
        ball->transform.position.y += ball_velocity.y * elapsed;
        ball->transform.position.z += ball_velocity.z * elapsed;

        bool player1_getting_point_prev = player1_getting_point;
        player1_getting_point = ball->transform.position.y <= -1.7f;
        if (player1_getting_point_prev != player1_getting_point) {
          num_bounces = 0;
        }
      }
		}

		//draw output:
		glClearColor(0.5, 0.5, 0.5, 0.0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);


		{ //draw game state:
			glUseProgram(program);
			glUniform3fv(program_to_light, 1, glm::value_ptr(glm::normalize(glm::vec3(0.0f, 1.0f, 10.0f))));
			scene.render();
		}

		SDL_GL_SwapWindow(window);
	}


	//------------  teardown ------------

	SDL_GL_DeleteContext(context);
	context = 0;

	SDL_DestroyWindow(window);
	window = NULL;

	return 0;
}



static GLuint compile_shader(GLenum type, std::string const &source) {
	GLuint shader = glCreateShader(type);
	GLchar const *str = source.c_str();
	GLint length = source.size();
	glShaderSource(shader, 1, &str, &length);
	glCompileShader(shader);
	GLint compile_status = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compile_status);
	if (compile_status != GL_TRUE) {
		std::cerr << "Failed to compile shader." << std::endl;
		GLint info_log_length = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_log_length);
		std::vector< GLchar > info_log(info_log_length, 0);
		GLsizei length = 0;
		glGetShaderInfoLog(shader, info_log.size(), &length, &info_log[0]);
		std::cerr << "Info log: " << std::string(info_log.begin(), info_log.begin() + length);
		glDeleteShader(shader);
		throw std::runtime_error("Failed to compile shader.");
	}
	return shader;
}

static GLuint link_program(GLuint fragment_shader, GLuint vertex_shader) {
	GLuint program = glCreateProgram();
	glAttachShader(program, vertex_shader);
	glAttachShader(program, fragment_shader);
	glLinkProgram(program);
	GLint link_status = GL_FALSE;
	glGetProgramiv(program, GL_LINK_STATUS, &link_status);
	if (link_status != GL_TRUE) {
		std::cerr << "Failed to link shader program." << std::endl;
		GLint info_log_length = 0;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &info_log_length);
		std::vector< GLchar > info_log(info_log_length, 0);
		GLsizei length = 0;
		glGetProgramInfoLog(program, info_log.size(), &length, &info_log[0]);
		std::cerr << "Info log: " << std::string(info_log.begin(), info_log.begin() + length);
		throw std::runtime_error("Failed to link program");
	}
	return program;
}
