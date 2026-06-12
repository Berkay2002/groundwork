# Cleaned Combined Transcript

I spent the last four years trying to make the best C++ Minecraft clone on YouTube, with multiplayer, super cool shaders, and world generation. During this time I learned so many interesting things and optimizations, so in this video I will tell you the full story to inspire you. I have seen many people try this challenge, and everyone makes the same mistakes I did when I first started, so I will also give you some important technical tips if you want to implement this yourself.

The story starts long ago, when I first saw this video by Hopson about making Minecraft in C++. Minecraft and C++ were my two favorite things, so I knew I also had to do it. I was just a beginner, so I started with a Terraria clone, then I learned OpenGL and slowly dived into 3D stuff.

Eventually, I made my first ever Minecraft clone. The world generator was quite nice. I assigned a different height to each biome, so I was able to get some super interesting, wacky terrain results. There were some big problems though: no multiplayer, no transparency, and the chunk system was super bad. So I started making a new Minecraft clone for a university assignment.

With the second project, I was actually able to figure out transparency and voxel ambient occlusion. However, this project was a university assignment, and I was basically forced to write the entire code in an OOP style, where everything had to be private. There was no way I would continue working on that codebase. Plus, I started to learn multiplayer programming in the meantime, and I realized that if I wanted my Minecraft game to be multiplayer, I had to implement that from the beginning.

So it was clear what I had to do. I had to start fresh: a new C++ Minecraft project, this time one that would be multiplayer. I promised myself it would be bigger than anything I had ever done before. In fact, bigger than any other project I had seen on YouTube.

At the end of 2021, I made the first commit to what would become my biggest C++ project ever, something I did not know yet. I added the very basic things, like blocks, then chunks, and optimizations like not drawing faces that are invisible, or only updating the GPU data when I modify the chunk. I got to this basic point, but now it was time to move this to multiplayer.

I have a quick tip here: do not attempt multiplayer if you have not done it before. Multiplayer is not an afterthought feature that you add later to your project. Your entire project has to be implemented in a special way for multiplayer to work. Even something as simple as item durability is super hard to implement in a multiplayer game.

But I did not know how hard things were going to be. Six months after the first commit, I had a server that I could connect to. The server would create chunks and give them to all the connected players. The players could also request chunks, and the server would give those chunks to the player. Most importantly, if one player placed a block, the server would see that and broadcast that block to the other connected players.

Things would become way more complex in the future for multiplayer, but for now I started to work on the world generator. If there is one thing I would change about Minecraft, it would be the world generator. I would love to have way more structures and cool randomly generated things, so I knew I had to implement a super good world generator.

I did my research by watching this video, and something got my attention. You probably already know that Minecraft uses noise to procedurally generate a world, but using noise alone will never get you super good results. I learned from this video that you need a way to shape the noise in various ways to get good results.

So I created a separate tool that would allow me to easily and visually customize the world generator noise using an interactive spline editor. From here, I can easily configure the noise for the world exactly how I want it, and then I wrote some code to export and load the settings in this text-based format. Now I can easily tweak the world settings, save them, and load the result in-game. This is super useful because it also means I can let the player tweak their world generation settings per world as much as they want.

I managed to get it to quite a good state, even challenging myself by implementing this cool jungle village that generates randomly. I have these premade trees, and I mark with this block where the start of a bridge is. Using this information, the server can place some random trees and join them using a bridge, all procedurally generated and random.

You probably also noticed that I improved the lighting significantly. Most C++ Minecraft clones on YouTube are super cool, but none of them have nice shaders. I spent a lot of time learning OpenGL, so I also wanted my game to have super cool shaders. I started with lights that were super hard to implement. Luckily, this video linked an article that explained how to do them step by step, and I eventually managed to implement them correctly after a lot of debugging.

Once I had light, I could also implement PBR into my game. PBR means physically based rendering, and it just means some fancy equations and materials that make your game look nice. You do need lights for this to work, and also PBR materials. Luckily, I had just implemented lights, and I also found a texture pack that has PBR information, so all I had to do was copy-paste the PBR code from my 3D game engine. The game was starting to look kind of cool.

I do not know why, but I also started to obsess over the water shader at that point. The water shader ended up being a super complex multi-step process just so I could implement this water wobble thing, but I think it looks super cool. Keep in mind, we are still in the beginning stages of the shaders, so the end result looks even cooler.

Now I think it would be a good time to start talking about some of the optimizations in this game. The server was really a mess, so I had to redesign it almost completely. The new server design allowed for some crazy multithreading optimizations and also a smoother development process, so let us talk about it a little.

The job of the server is to simulate the world and send updates to all connected players. Also, when a player makes any changes, the server has to accept that change and notify everyone about it, or reject it and notify the player to undo the change.

Initially, clients could request chunks from the server as they moved around the world, but it was all a mess. I realized that since the server has to simulate the world, it needs to clearly know what client has what chunk. For example, if a player has a super small render distance, the server only has to simulate a few chunks. So now the client tells the server where he is and what render distance he has, and the server gives him chunks starting with the closest chunk first.

Generating chunks is quite expensive, so I made sure the server can only generate a small number of chunks per frame. If I just send one chunk to the first player that does not have chunks, it will keep sending chunks to the first player while the other players have to wait for a long time. I do not want that, so I also shuffle the player list randomly every frame. In this way, everyone will get chunks eventually. Also, if a player happens to miss the chunk he is in, I always send it.

The chunks are also zipped before they are sent, so they occupy less space. After testing it with a friend, the round trip from Chile to Romania and all the way back took around 0.2 seconds for a player to request and receive chunks.

The server optimizations do not stop here though. With the server refactor, I was later able to multithread it to the point that each player can have its own thread. But before I got there, I also needed to implement entities, so we will talk about this optimization later.

I also simplified the chunk logic both on the client and the server. Right now, the chunk system on the server is just an unordered map that has the chunk position as a key and the chunk as a value. For the client, I just store the chunks in a 2D array. If you want to make this project yourself, this is by far the best way to implement this.

Now I also need to talk about rendering optimizations, starting with probably the most important one: vertex pulling. I realized quite early that all I do is render block faces, so why should I bother sending all this geometry for every face? Is there not a way to tell the GPU to just draw a face for me at this position? I figured out a way to do this, and I later learned that it is called vertex pulling.

To give you an idea of how powerful my implementation is, traditionally this face would be made out of four vertices. Each vertex would have three floats for position, three floats for normal, two floats for UV, and probably some extra data for the material, texture flags, and light level. This means 32 floats of mostly redundant information, plus somewhere to store the material information.

With my implementation, all I need is four floats to render an entire face. With only four floats, I basically tell the GPU where to draw the face, what face shape to draw, what texture to use, the light levels, and even flags like whether the face is water or has a custom color. Using the shape index, I look into a buffer that gives me the shape of the block.

This optimization is insanely big, but it would cause me some headaches later when I wanted to implement furniture. We are not there yet. Right now, I have this super cool optimization plus others, like Z pre-pass and frustum culling, and I later also implemented LOD. With this, I can have a big render distance, like 20 chunks, while also having super cool shaders.

I will add only a small tip here: do not implement greedy meshing. Trust me, it is a horrible idea. If you want to get a full explanation, watch this video.

On the surface, the game started to look super nice, and I even started chatting with various Minecraft shader developers to get some tips on making the game look better. In reality, I had to implement entities to really progress in the game, and they were super hard because of the multiplayer aspect.

At the beginning of 2024, I started to implement my first entity: the dropped item. The reason this is so hard is because the entity needs to fall on the client that threw it, on the server, and also on the other clients. Somehow, everyone needs to see the entity fall smoothly while the server always updates the entity. This was hard in the beginning, but it eventually worked out nicely.

Now that I had this entity code working, I realized I had another super big problem. Quick tip: this is why I always tell you to do prototype-based design. This means write the code first, see how it goes, and then refactor it and abstract it into a better system. You can see the full complexity of a system only once you write it, not just by planning ahead.

The problem here was that while the system was working, it was a lot of work to add an entity. More importantly, there was a lot of repetitive code that I did not want to copy-paste. I started with something simple, like pigs and cats, then I hardcoded their leg animations so I could see how things go.

I managed to invent something that I do not think was ever done before, and I call it a compile-time entity component system. The way I hold entities in my game is that I have a different container for each entity type. So I have an unordered map of zombies, an unordered map of dropped items, and so on. You might think this is weird, but it is actually the most goated implementation ever.

Every entity is just a simple struct, and I do not need to use polymorphism. I can also embed the entity type in the ID, so I can search for any entity super fast. Updating entities is also super fast, again with no polymorphism, so the compiler can go wild with the optimizations.

There is only one problem: I need to duplicate a ton of code with this approach. Any time I add a new entity, I need to add a new container and then copy-paste code to iterate over it in many places. But what if I wrote code that did this for me? Imagine I wrote some code that could see that I added a new entity struct and update itself everywhere it needs to.

This is called metaprogramming, and C++ does not really have strong metaprogramming features. But using some questionable macros and also some questionable template code, I created systems that automatically iterate over all entities. This is weird, but I only need to write this system once, and then any time I add a new entity, it will automatically be added everywhere in the code.

This will generate a different for loop for each entity, and the compiler will optimize each of them individually. For example, dropped items can push other entities, so the physics code will skip the container with dropped items completely. Or this training dummy does not have any server update code, so the compiler can optimize that away completely.

The result is super nice. I have the shared data that is synchronized and managed automatically by the server, and then I have the server-specific and client-specific implementation of that entity. Then I can add special flags. For example, this one makes the entity push other entities, so the physics code knows to also check against pigs for entity collisions. Again, all entities that do not have these flags are entirely skipped in that part of the physics update. I do not even iterate over those containers in the first place.

All I had to do now was load the entity models and animate them. I use Blockbench to make models, and I load them using Assimp. Then I implemented the super rudimentary animation system. The animation states are actually managed and synchronized by the server, so the client will receive the entity animation state from the server when updating the entity on the client, and even do predictions, like always adding the falling animation if needed without needing the server. All I have to do to enable animations is place this flag in the entity, and this will add the animation system to both the client and the server.

I even implemented super cool enemy AI. The enemy can hear you, and he actually has to see you before he starts to chase you. But these cool enemy models made me wonder: what if I could also load custom block models?

By this point, the game started to take its own identity. I even had some people who wanted to contribute art: this guy who made the goblin model, and this guy who made some sounds and music for the game. That is why you see the super cool custom sprites for the game.

For the gameplay, I decided to make the game just like Terraria, but in 3D. This meant lots of cool custom geometry and custom structures to find. If you remember, when I talked about vertex pulling, I mentioned something about face shapes. Because of how I have my rendering code, I can only render predefined faces. But I realized I can just make the model, export it, then figure out what the faces are for that model when I load the game, and place them in the big GPU buffer with faces. I do that again for every rotation of the model.

With that, I can render custom blocks in-game, even with rotations. In a Terraria-like fashion, this is a goblin workbench, and I can use it to craft goblin furniture. They are also paintable. I made every block paintable, including leaves or even water.

With custom geometry, buildings really start coming to life, so I added the structure block that I can use to save custom structures, like this whole house or other structures that would randomly spawn in the world. This might seem simple to implement, but remember, this is a multiplayer game. This block has to be synchronized with the server, meaning I had to implement the ability to have blocks synchronized with the server. So I did that, and also block entities like this training dummy that I can hit. Using these new systems, I would later be able to implement chests as well, and I am super happy I had them.

Before I talk about the multithreading optimizations, by far the number one thing I think Minecraft is missing is procedural dungeons. The first thing I did was create some dungeon hallways and some dungeon rooms that can be randomly connected and placed in the world to create a dungeon. They are also made by a block that can be mined in survival, similarly to the Terraria dungeon, so you cannot cheese the dungeon like you can in Minecraft. You really have to fight your way through the dungeon.

Speaking about survival, I started to slowly implement it. I made server commands just for fun, and you can set your game mode to survival. Enemies have to see you to attack you, and you can use a dagger to do a surprise attack on them, adding the possibility of classes to the game. But be careful, they can hear you and turn around. I also added block mining duration, death, potions, and effects. The development process was quite slow because of the multiplayer aspect, but I would say almost all survival features are at least in a decent state.

I also experimented with the world generator a lot. Would it not be cool if each region of the world had a specific difficulty, and some could even have names and custom enemy fortresses inside that you need to fight? So I made a world that splits the biomes into clear regions that all have a specific height, and I also know where the center of that region is, so I can later potentially spawn the big enemy dungeon there. What if, once you defeat the enemy dungeon, the area becomes less hostile and you can make houses for NPCs there, like in Terraria?

Speaking of the world generator, I improved it a lot, and same for shaders. I made roads that in the future will maybe connect points of interest, and rivers that can even carve through mountains, making a super cool boat passage. I added many things that can spawn in the world, and many types of geometry features that can spawn in any biome type. They made the mountains sometimes super wacky. I love it.

I also added all the basic shader features you might want, like shadows, super cool water, lens flare, god rays, and many small tweaks that make the game look so good. But before we take a look at the final result, let us talk about the two multithreading optimizations I have. They basically allow each client to potentially have its own thread on the server.

The first one is computing the chunk geometry on the client on multiple threads. People view multithreading very wrong. You do not just want to share your data and use mutexes. You want to organize your data so that you do not need any mutex. In this case, I have some workers. Each worker will compute one chunk along with the main thread, and once everyone is done, I can send the data to the GPU from the main thread and continue with my frame.

But the multithreading optimization on the server is even more insane. I realized that if two players are far apart, meaning they have some unloaded chunks between them, they can easily be simulated on different threads with no communication and no mutex between them.

Again, people get multithreading very wrong. Everyone asks me how I join and split regions, and if that is expensive. Well, I do not even have to join or split regions, and the secret is in organizing your data right. Every chunk is self-contained, meaning it stores its blocks and entities. If an entity moves to another chunk, it does mean I need to move the ownership of it to the new chunk. Annoying, but not really a big deal.

Since every chunk is self-contained, the server just has to calculate what the individual regions are and send a pointer to those chunks to a worker thread. Those worker threads will just do the updates normally and also respond to networking events. Once every worker is done, there is super minimal cleanup remaining to do.

This is how the project looks now. Right now, I am quite burned out after working on it for so long, so it will take some more time until I update this project. But this was by far the coolest thing I ever did, especially the multiplayer aspect that I barely touched on in this video.

If you want to make a Minecraft clone in C++ yourself, I will release a technical video with all the tips that you need. Trust me, you do not want to miss it. I have seen the same mistakes people make time and time again. Also, if you have not made a big game before, you should start with something simpler like Terraria. If you need help there, you can check out my course: six hours of edited video content and 50-plus challenges. I worked half a year to make this course. Link in the description.

If you want to see more of this project, I have an entire playlist linked in the description. Make sure you check those videos. See you there.
