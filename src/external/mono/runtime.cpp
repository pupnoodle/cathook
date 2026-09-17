#include "runtime.hpp"

#include <utility>

namespace mono
{
runtime::~runtime()
{
	shutdown();
}

bool runtime::initialize(backend value, const std::function<bool(ImGuiIO &)> &load_fonts)
{
	if (m_initialized) {
		return true;
	}
	if (!ImGui::CreateContext()) {
		return false;
	}
	m_backend = std::move(value);
	if ((m_backend.initialize && !m_backend.initialize()) || (load_fonts && !load_fonts(ImGui::GetIO()))) {
		if (m_backend.shutdown) {
			m_backend.shutdown();
		}
		m_backend = {};
		ImGui::DestroyContext();
		return false;
	}
	apply_layout();
	m_initialized = true;
	return true;
}

void runtime::shutdown()
{
	if (!m_initialized) {
		return;
	}
	if (m_backend.shutdown) {
		m_backend.shutdown();
	}
	ImGui::DestroyContext();
	m_backend = {};
	m_initialized = false;
}

void runtime::abandon()
{
	if (!m_initialized) {
		return;
	}

	ImGui::DestroyContext();
	m_backend = {};
	m_initialized = false;
}

void runtime::begin_frame(const color accent)
{
	if (!m_initialized) {
		return;
	}
	apply_colors(accent);
	if (m_backend.new_frame) {
		m_backend.new_frame();
	}
	ImGui::NewFrame();
}

void runtime::end_frame()
{
	if (!m_initialized) {
		return;
	}
	ImGui::EndFrame();
	ImGui::Render();
	if (m_backend.render) {
		m_backend.render(ImGui::GetDrawData());
	}
}
}
