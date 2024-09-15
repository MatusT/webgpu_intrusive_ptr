#include "plf_hive.hpp"
#include "singleton_atomic.hpp"

#include <boost/intrusive_ptr.hpp>

#include <atomic>
#include <cstdint>
#include <print>

/// This file expands the previous example by creating Singleton for all
/// resources so that a resoure does not have store pointer to the data
/// structure but intrusive_ptr_release can reference it.

// https://www.w3.org/TR/webgpu/#dom-gpubuffer-internal-state-slot
enum class TextureInternalState {
  Available,
  Unavailable,
  Destroyed,
};

// Have some class that can hold Vulkan object to be destroyed
struct TextureToBeDestroyed {
  // std::uint64_t submissionIndex;
  // vk::Texture vulkanTexture;
};

// This is now our custom implementation. intrusive_ptr_release relies on
// Resources singleton. It can be made into a template for easy writing just as
// boost::intrusive_ptr
class Texture {
public:
  Texture() noexcept { std::println("Texture::Constructor"); }

#pragma region noncopyable
  Texture(const Texture &) = delete;
  Texture &operator=(const Texture &) = delete;
  Texture(Texture &&) = delete;
  Texture &operator=(Texture &&) = delete;
#pragma ednregion noncopyable

  // Follow: https://www.w3.org/TR/webgpu/#buffer-destruction
  // Notice: no class destructor is called, this doesn't release CPU side
  // object!
  void destroy() {
    if (mInternalState == TextureInternalState::Destroyed) {
      // Valid according to the specification. Nothing to do.
      return;
    }

    // Unmap
    // ...

    // Set state to destroyed
    mInternalState = TextureInternalState::Destroyed;

    // If this was mappable buffer it could have had staging buffer that can be
    // deleted immediately. if (stagingBuffer) delete staging;

    // Enqueue GPU Memory destruction
    // ^ A few design decisions to be made. This can be a lot of things:
    // - some async task running in another thread(s). Here comes handy the
    // thread safety of plf::hive ;-)
    //   Pseudocode:
    //    thread/task_pool::fire_and_forget(() => {
    //      wait(lastSubmissionIndex);
    //      deviceDeleteTexture(texture);
    //    });
    // - just a vector that is evaluated at each submit
    //   Pseudocode:
    //     queue.texturesToBeDestroyed.push_back({ .texture = mTexture; });
    // ...
  }

  virtual ~Texture() {
    // Enqueue GPU memory destruction if explicit destroy() call was not made
    if (mInternalState != TextureInternalState::Destroyed) {
      destroy();
    }

    // Continue with destruction of actual CPU object of the implementation
    std::println("Texture::Destructor with count {0}", mRefCounter.load());
  }

protected:
  TextureInternalState mInternalState{TextureInternalState::Available};
  mutable std::atomic<std::uint64_t> mRefCounter{0};

protected:
  friend void intrusive_ptr_add_ref(const Texture *p) noexcept;
  friend void intrusive_ptr_release(const Texture *p) noexcept;
};

#pragma region Singleton Resource Hub
class Resources : public SingletonAtomic<Resources> {
public:
  Resources() {}

  boost::intrusive_ptr<Texture> createTexture() {
    boost::intrusive_ptr<Texture> texture{&(*mTextures.emplace())};

    return texture;
  }

  // plf::hive has several nice properties for storage
  // - stable pointers this is a trick that can help us
  //  1. make maps std::map<resource, state> memory safe!
  //  2. implementation of intrusive_ptr_release super easy
  // - compact storage
  // - simultaneous thread-safe creation, access, erasure
  plf::hive<Texture> &getTextures() { return mTextures; }

private:
  plf::hive<Texture> mTextures;
};
#pragma endregion Singleton Resource Hub

inline void intrusive_ptr_add_ref(const Texture *p) noexcept {
  p->mRefCounter++;
}

inline void intrusive_ptr_release(const Texture *p) noexcept {
  if (--(p->mRefCounter) == 0) {
    auto &textures = Resources::GetInstance()->getTextures();
    textures.erase(textures.get_iterator(p));
  }
}

// This part shows how raw native WebGPU functions can be now implemented using
// above functionality.
#pragma region WebGPU
Texture *wgpuInstanceRequestTexture() {
  boost::intrusive_ptr<Texture> texture{
      Resources::GetInstance()->createTexture()};

  // Because WebGPU functions do NOT return intrusive_ptr (that will be
  // destroyed at the end of this function) but raw pointer we artificially
  // increase reference. It will be 2 and 1 after end of this function
  intrusive_ptr_add_ref(texture.get());

  return texture.get();
}

void wgpuTextureDestroy(Texture *texture) { texture->destroy(); }
void wgpuTextureAddRef(Texture *texture) { intrusive_ptr_add_ref(texture); }
void wgpuTextureRelease(Texture *texture) { intrusive_ptr_release(texture); }
#pragma endregion WebGPU

int main() {
  Resources::Construct();
  auto texture = Resources::GetInstance()->createTexture();

  std::println("the end");
}