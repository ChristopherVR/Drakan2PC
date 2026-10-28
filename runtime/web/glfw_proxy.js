// DOM, GLFW and WebGL state belongs to the browser thread. The application
// pthread only sends synchronous calls and receives scalar results.
for (var drakanName of Object.keys(LibraryManager.library)) {
  if (/^(glfw[A-Z]|emscripten_gl[A-Z]|gl[A-Z])/.test(drakanName) &&
      typeof LibraryManager.library[drakanName] === 'function') {
    LibraryManager.library[drakanName + '__proxy'] = 'sync';
  }
}
// GLFW's normal Web implementation has an empty swap function. With proxied GL
// calls, render into the SDK's offscreen back buffer and present once per frame.
#if OFFSCREEN_FRAMEBUFFER
LibraryManager.library.glfwSwapBuffers = function(winid) {
  if (GL.currentContext && GL.currentContext.defaultFbo)
    GL.blitOffscreenFramebuffer(GL.currentContext);
};
LibraryManager.library.glfwSwapBuffers__proxy = 'sync';
LibraryManager.library.glfwSwapBuffers__deps = ['$GL'];
#endif

// Clock queries have no DOM state. Raylib's frame wait polls this frequently;
// proxying each query can stall input/presentation behind a synchronous UI call.
// The C++ clock has one shared origin and offset for all pthreads and callbacks.
LibraryManager.library.glfwGetTime = function() { return _drakan_web_glfw_time(); };
LibraryManager.library.glfwGetTime__sig = 'd';
LibraryManager.library.glfwGetTime__deps = ['drakan_web_glfw_time'];
delete LibraryManager.library.glfwGetTime__proxy;
LibraryManager.library.glfwSetTime = function(value) { _drakan_web_glfw_set_time(value); };
LibraryManager.library.glfwSetTime__sig = 'vd';
LibraryManager.library.glfwSetTime__deps = ['drakan_web_glfw_set_time'];
delete LibraryManager.library.glfwSetTime__proxy;
