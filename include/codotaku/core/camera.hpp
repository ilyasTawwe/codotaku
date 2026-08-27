#pragma once

#include <cmath>
#include <codotaku/core/types.hpp>

namespace codotaku {

class Camera {
public:
    Camera(glm::vec3 position = {0.0f, 1.2f, 2.8f},
           glm::vec3 target = {0.0f, 0.0f, 0.0f},
           glm::vec3 up = {0.0f, 1.0f, 0.0f},
           float fov_y_degrees = 45.0f,
           float aspect_ratio = 16.0f / 9.0f,
           float near_plane = 0.1f,
           float far_plane = 100.0f)
        : m_position(position),
          m_target(target),
          m_up(up),
          m_fov_y(glm::radians(fov_y_degrees)),
          m_aspect_ratio(aspect_ratio),
          m_near(near_plane),
          m_far(far_plane) {}

    void set_aspect_ratio(float aspect) {
        m_aspect_ratio = (aspect > 0.0f) ? aspect : 1.0f;
    }

    void set_fov_degrees(float fov_degrees) {
        m_fov_y = glm::radians(fov_degrees);
    }

    void set_clipping(float near_plane, float far_plane) {
        m_near = near_plane;
        m_far = far_plane;
    }

    void look_at(glm::vec3 position, glm::vec3 target, glm::vec3 up = {0.0f, 1.0f, 0.0f}) {
        m_position = position;
        m_target = target;
        m_up = up;
    }

    void set_position(glm::vec3 pos) { m_position = pos; }
    void set_target(glm::vec3 target) { m_target = target; }

    glm::vec3 get_position() const { return m_position; }
    glm::vec3 get_target() const { return m_target; }
    glm::vec3 get_up() const { return m_up; }
    float get_aspect_ratio() const { return m_aspect_ratio; }

    glm::mat4 get_view_matrix() const {
        return glm::lookAt(m_position, m_target, m_up);
    }

    glm::mat4 get_projection_matrix() const {
        glm::mat4 proj = glm::perspective(m_fov_y, m_aspect_ratio, m_near, m_far);
        proj[1][1] *= -1.0f; // Invert Y for Vulkan NDC clip space
        return proj;
    }

    glm::mat4 get_view_projection_matrix() const {
        return get_projection_matrix() * get_view_matrix();
    }

    void orbit(float delta_yaw, float delta_pitch) {
        glm::vec3 dir = m_position - m_target;
        float radius = glm::length(dir);
        if (radius < 0.001f) radius = 1.0f;

        float yaw = std::atan2(dir.x, dir.z) + delta_yaw;
        float pitch = std::asin(std::clamp(dir.y / radius, -0.99f, 0.99f)) + delta_pitch;
        pitch = std::clamp(pitch, glm::radians(-89.0f), glm::radians(89.0f));

        m_position = m_target + glm::vec3(
            radius * std::cos(pitch) * std::sin(yaw),
            radius * std::sin(pitch),
            radius * std::cos(pitch) * std::cos(yaw)
        );
    }

    void zoom(float delta_radius) {
        glm::vec3 dir = m_position - m_target;
        float radius = glm::length(dir);
        radius = std::max(0.1f, radius + delta_radius);
        m_position = m_target + glm::normalize(dir) * radius;
    }

private:
    glm::vec3 m_position{0.0f, 1.2f, 2.8f};
    glm::vec3 m_target{0.0f, 0.0f, 0.0f};
    glm::vec3 m_up{0.0f, 1.0f, 0.0f};

    float m_fov_y{glm::radians(45.0f)};
    float m_aspect_ratio{16.0f / 9.0f};
    float m_near{0.1f};
    float m_far{100.0f};
};

} // namespace codotaku
