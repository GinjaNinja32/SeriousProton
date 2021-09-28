#include "graphics/renderTarget.h"
#include "graphics/textureAtlas.h"
#include "textureManager.h"
#include "windowManager.h"
#include "engine.h"

#include "GL/glew.h"
#include <glm/gtc/type_ptr.hpp>


namespace sp {

static sp::Font* default_font = nullptr;

static sp::Shader* shader = nullptr;
static unsigned int vertices_vbo = 0;
static unsigned int indices_vbo = 0;

struct VertexData
{
    glm::vec2 position;
    glm::u8vec4 color;
    glm::vec2 uv;
};
static std::vector<VertexData> vertex_data;
static std::vector<uint16_t> index_data;

static sp::AtlasTexture* atlas_texture;
static std::unordered_map<string, Rect> atlas_images;
static std::unordered_map<sp::Font*, std::unordered_map<int, Rect>> atlas_glyphs;

static Rect getFromAtlas(std::string_view texture)
{
    auto it = atlas_images.find(texture);
    if (it != atlas_images.end())
        return it->second;
    Image image;
    auto stream = getResourceStream(texture);
    if (!stream)
        stream = getResourceStream(string(texture) + ".png");
    image.loadFromStream(stream);
    Rect uv_rect = atlas_texture->add(std::move(image), 1);
    atlas_images[texture] = uv_rect;
    LOG(Info, "Added ", string(texture), " to atlas@", uv_rect.position, " ", uv_rect.size, "  ", atlas_texture->usageRate() * 100.0f, "%");
    return uv_rect;
}

RenderTarget::RenderTarget(glm::vec2 virtual_size, glm::ivec2 physical_size)
: virtual_size(virtual_size), physical_size(physical_size)
{
    if (!shader)
        shader = new sp::Shader(R"(
#version 120
uniform mat3 u_projection;

attribute vec2 a_position;
attribute vec2 a_texcoords;
attribute vec4 a_color;

varying vec2 v_texcoords;
varying vec4 v_color;

void main()
{
    v_texcoords = a_texcoords;
    v_color = a_color;
    gl_Position = vec4(u_projection * vec3(a_position, 1.0), 1.0);
}
)", R"(
#version 120
uniform sampler2D u_texture;

varying vec2 v_texcoords;
varying vec4 v_color;

void main()
{
    gl_FragColor = texture2D(u_texture, v_texcoords) * v_color;
    gl_FragColor.rgb *= v_color.a;
}
)");
    if (!vertices_vbo)
    {
        glGenBuffers(1, &vertices_vbo);
        glGenBuffers(1, &indices_vbo);
    }
    shader->bind();

    glm::mat3 project_matrix{1.0f};
    project_matrix[0][0] = 2.0f / float(virtual_size.x);
    project_matrix[1][1] = -2.0f / float(virtual_size.y);
    project_matrix[2][0] = -1.0f;
    project_matrix[2][1] = 1.0f;
    glUniformMatrix3fv(shader->getUniformLocation("u_projection"), 1, GL_FALSE, glm::value_ptr(project_matrix));

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (!atlas_texture)
        atlas_texture = new AtlasTexture({4096, 4096});
}

void RenderTarget::setDefaultFont(sp::Font* font)
{
    default_font = font;
}

void RenderTarget::drawSprite(std::string_view texture, glm::vec2 center, float size, glm::u8vec4 color)
{
    sp::Rect uv_rect = getFromAtlas(texture);

    int n = vertex_data.size();
    index_data.insert(index_data.end(), {
        n + 0, n + 1, n + 2,
        n + 1, n + 3, n + 2,
    });
    size *= 0.5f;
    glm::vec2 offset{size / uv_rect.size.y * uv_rect.size.x, size};
    vertex_data.push_back({
        {center.x - offset.x, center.y - offset.y},
        color, {uv_rect.position.x, uv_rect.position.y}});
    vertex_data.push_back({
        {center.x - offset.x, center.y + offset.y},
        color, {uv_rect.position.x, uv_rect.position.y + uv_rect.size.y}});
    vertex_data.push_back({
        {center.x + offset.x, center.y - offset.y},
        color, {uv_rect.position.x + uv_rect.size.x, uv_rect.position.y}});
    vertex_data.push_back({
        {center.x + offset.x, center.y + offset.y},
        color, {uv_rect.position.x + uv_rect.size.x, uv_rect.position.y + uv_rect.size.y}});
}

void RenderTarget::drawRotatedSprite(std::string_view texture, glm::vec2 center, float size, float rotation, glm::u8vec4 color)
{
    if (rotation == 0)
        return drawSprite(texture, center, size, color);
    sp::Rect uv_rect = getFromAtlas(texture);

    int n = vertex_data.size();
    index_data.insert(index_data.end(), {
        n + 0, n + 1, n + 2,
        n + 1, n + 3, n + 2,
    });
    size *= 0.5f;
    glm::vec2 offset0 = rotateVec2({size / uv_rect.size.y * uv_rect.size.x, size}, rotation);
    glm::vec2 offset1{offset0.y, -offset0.x};
    vertex_data.push_back({
        center - offset0,
        color, {uv_rect.position.x, uv_rect.position.y}});
    vertex_data.push_back({
        center - offset1,
        color, {uv_rect.position.x, uv_rect.position.y + uv_rect.size.y}});
    vertex_data.push_back({
        center + offset1,
        color, {uv_rect.position.x + uv_rect.size.x, uv_rect.position.y}});
    vertex_data.push_back({
        center + offset0,
        color, {uv_rect.position.x + uv_rect.size.x, uv_rect.position.y + uv_rect.size.y}});
}

void RenderTarget::drawRotatedSpriteBlendAdd(std::string_view texture, glm::vec2 center, float size, float rotation)
{
    /*
    sf::Sprite sprite;
    textureManager.setTexture(sprite, texture);
    sprite.setPosition(sf::Vector2f(center.x, center.y));
    sprite.setScale(size / sprite.getTextureRect().height, size / sprite.getTextureRect().height);
    sprite.setRotation(rotation);
    target.draw(sprite, sf::BlendAdd);
    */
}

void RenderTarget::drawLine(glm::vec2 start, glm::vec2 end, glm::u8vec4 color)
{
    /*
    sf::VertexArray a(sf::LinesStrip, 2);
    a[0].position.x = start.x;
    a[0].position.y = start.y;
    a[1].position.x = end.x;
    a[1].position.y = end.y;
    a[0].color = sf::Color(color.r, color.g, color.b, color.a);
    a[1].color = sf::Color(color.r, color.g, color.b, color.a);
    target.draw(a);
    */
}

void RenderTarget::drawLine(glm::vec2 start, glm::vec2 end, glm::u8vec4 start_color, glm::u8vec4 end_color)
{
    /*
    sf::VertexArray a(sf::LinesStrip, 2);
    a[0].position.x = start.x;
    a[0].position.y = start.y;
    a[1].position.x = end.x;
    a[1].position.y = end.y;
    a[0].color = sf::Color(start_color.r, start_color.g, start_color.b, start_color.a);
    a[1].color = sf::Color(end_color.r, end_color.g, end_color.b, end_color.a);
    target.draw(a);
    */
}

void RenderTarget::drawLine(const std::initializer_list<glm::vec2> points, glm::u8vec4 color)
{
    /*
    sf::VertexArray a(sf::LinesStrip, points.size());
    int n=0;
    for(auto point : points)
    {
        a[n].position.x = point.x;
        a[n].position.y = point.y;
        a[n].color = sf::Color(color.r, color.g, color.b, color.a);
        n++;
    }
    target.draw(a);
    */
}

void RenderTarget::drawLine(const std::vector<glm::vec2> points, glm::u8vec4 color)
{
    /*
    sf::VertexArray a(sf::LinesStrip, points.size());
    int n=0;
    for(auto point : points)
    {
        a[n].position.x = point.x;
        a[n].position.y = point.y;
        a[n].color = sf::Color(color.r, color.g, color.b, color.a);
        n++;
    }
    target.draw(a);
    */
}

void RenderTarget::drawLineBlendAdd(const std::vector<glm::vec2> points, glm::u8vec4 color)
{
    /*
    sf::VertexArray a(sf::LinesStrip, points.size());
    int n=0;
    for(auto point : points)
    {
        a[n].position.x = point.x;
        a[n].position.y = point.y;
        a[n].color = sf::Color(color.r, color.g, color.b, color.a);
        n++;
    }
    target.draw(a, sf::RenderStates(sf::BlendAdd));
    */
}

void RenderTarget::drawPoint(glm::vec2 position, glm::u8vec4 color)
{
    /*
    sf::VertexArray a(sf::Points, 1);
    a[0].position.x = position.x;
    a[0].position.y = position.y;
    a[0].color = sf::Color(color.r, color.g, color.b, color.a);
    target.draw(a);
    */
}

void RenderTarget::drawRectColorMultiply(const sp::Rect& rect, glm::u8vec4 color)
{
    /*
    sf::RectangleShape overlay(sf::Vector2f(rect.size.x, rect.size.y));
    overlay.setPosition(rect.position.x, rect.position.y);
    overlay.setFillColor(sf::Color(color.r, color.g, color.b, color.a));
    target.draw(overlay, sf::BlendMultiply);
    */
}

void RenderTarget::drawCircleOutline(glm::vec2 center, float radius, float thickness, glm::u8vec4 color)
{
    /*
    sf::CircleShape circle(radius - thickness, 50);
    circle.setOrigin(radius - thickness, radius - thickness);
    circle.setPosition(center.x, center.y);
    circle.setFillColor(sf::Color::Transparent);
    circle.setOutlineThickness(thickness);
    circle.setOutlineColor(sf::Color(color.r, color.g, color.b, color.a));
    target.draw(circle);
    */
}

void RenderTarget::drawTiled(const sp::Rect& rect, std::string_view texture)
{
    /*
    sf::RectangleShape overlay(sf::Vector2f(rect.size.x, rect.size.y));
    overlay.setPosition(rect.position.x, rect.position.y);
    overlay.setTexture(textureManager.getTexture(texture));
    P<WindowManager> window_manager = engine->getObject("windowManager");
    sf::Vector2i texture_size = window_manager->mapCoordsToPixel(sf::Vector2f(rect.size.x, rect.size.y)) - window_manager->mapCoordsToPixel(sf::Vector2f(0, 0));
    overlay.setTextureRect(sf::IntRect(0, 0, texture_size.x, texture_size.y));
    target.draw(overlay);
    */
}

void RenderTarget::drawTriangleStrip(const std::initializer_list<glm::vec2>& points, glm::u8vec4 color)
{
    /*
    sf::VertexArray a(sf::TrianglesStrip, points.size());
    int n=0;
    for(auto point : points)
    {
        a[n].position.x = point.x;
        a[n].position.y = point.y;
        a[n].color = sf::Color(color.r, color.g, color.b, color.a);
        n++;
    }
    target.draw(a);
    */
}

void RenderTarget::drawTriangleStrip(const std::vector<glm::vec2>& points, glm::u8vec4 color)
{
    /*
    sf::VertexArray a(sf::TrianglesStrip, points.size());
    int n=0;
    for(auto point : points)
    {
        a[n].position.x = point.x;
        a[n].position.y = point.y;
        a[n].color = sf::Color(color.r, color.g, color.b, color.a);
        n++;
    }
    target.draw(a);
    */
}

void RenderTarget::fillCircle(glm::vec2 center, float radius, glm::u8vec4 color)
{
    /*
    sf::CircleShape circle(radius, 50);
    circle.setOrigin(radius, radius);
    circle.setPosition(center.x, center.y);
    circle.setFillColor(sf::Color(color.r, color.g, color.b, color.a));
    target.draw(circle);
    */
}

void RenderTarget::fillRect(const sp::Rect& rect, glm::u8vec4 color)
{
    /*
    sf::RectangleShape shape(sf::Vector2f(rect.size.x, rect.size.y));
    shape.setPosition(rect.position.x, rect.position.y);
    shape.setFillColor(sf::Color(color.r, color.g, color.b, color.a));
    target.draw(shape);
    */
}


void RenderTarget::drawTexturedQuad(std::string_view texture,
    glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3,
    glm::vec2 uv0, glm::vec2 uv1, glm::vec2 uv2, glm::vec2 uv3,
    glm::u8vec4 color)
{
    /*
    auto tex = textureManager.getTexture(texture);
    sf::VertexArray a(sf::TrianglesFan, 4);
    a[0].position = sf::Vector2f(p0.x, p0.y);
    a[1].position = sf::Vector2f(p1.x, p1.y);
    a[2].position = sf::Vector2f(p2.x, p2.y);
    a[3].position = sf::Vector2f(p3.x, p3.y);
    a[0].texCoords = sf::Vector2f(uv0.x * tex->getSize().x, uv0.y * tex->getSize().y);
    a[1].texCoords = sf::Vector2f(uv1.x * tex->getSize().x, uv1.y * tex->getSize().y);
    a[2].texCoords = sf::Vector2f(uv2.x * tex->getSize().x, uv2.y * tex->getSize().y);
    a[3].texCoords = sf::Vector2f(uv3.x * tex->getSize().x, uv3.y * tex->getSize().y);
    a[0].color = sf::Color(color.r, color.g, color.b, color.a);
    a[1].color = sf::Color(color.r, color.g, color.b, color.a);
    a[2].color = sf::Color(color.r, color.g, color.b, color.a);
    a[3].color = sf::Color(color.r, color.g, color.b, color.a);
    target.draw(a, tex);
    */
}

void RenderTarget::drawText(sp::Rect rect, std::string_view text, Alignment align, float font_size, sp::Font* font, glm::u8vec4 color)
{
    if (!font)
        font = default_font;
    auto& ags = atlas_glyphs[font];
    auto prepared = font->prepare(text, 32, font_size, rect.size, align);
    int flags = 0;

    float size_scale = font_size / 32.0f;
    for(auto gd : prepared.data)
    {
        Font::GlyphInfo glyph;
        if (gd.char_code == 0 || !font->getGlyphInfo(gd.char_code, 32, glyph))
        {
            glyph.advance = 0.0f;
            glyph.bounds.size.x = 0.0f;
        }

        if (glyph.bounds.size.x > 0.0f)
        {
            Rect uv_rect;
            auto it = ags.find(gd.char_code);
            if (it == ags.end())
            {
                uv_rect = atlas_texture->add(std::move(font->drawGlyph(gd.char_code, 32)), 1);
                ags[gd.char_code] = uv_rect;
                LOG(Info, "Added glyph '", char(gd.char_code), "' to atlas@", uv_rect.position, " ", uv_rect.size, "  ", atlas_texture->usageRate() * 100.0f, "%");
            }
            else
            {
                uv_rect = it->second;
            }

            float u0 = uv_rect.position.x;
            float v0 = uv_rect.position.y;
            float u1 = uv_rect.position.x + uv_rect.size.x;
            float v1 = uv_rect.position.y + uv_rect.size.y;
            
            float left = gd.position.x + glyph.bounds.position.x * size_scale;
            float right = left + glyph.bounds.size.x * size_scale;
            float top = gd.position.y - glyph.bounds.position.y * size_scale;
            float bottom = top + glyph.bounds.size.y * size_scale;
            
            if (flags & Font::FlagClip)
            {
                if (right < 0)
                    continue;
                if (left < 0)
                {
                    u0 = u1 - uv_rect.size.x * (0 - right) / (left - right);
                    left = 0;
                }

                if (left > rect.size.x)
                    continue;
                if (right > rect.size.x)
                {
                    u1 = u0 + uv_rect.size.x * (rect.size.x - left) / (right - left);
                    right = rect.size.x;
                }

                if (top < 0)
                    continue;
                if (bottom < 0)
                {
                    v1 = v0 + uv_rect.size.y * (0 - top) / (bottom - top);
                    bottom = 0;
                }

                if (bottom > rect.size.y)
                    continue;
                if (top > rect.size.y)
                {
                    v0 = v1 - uv_rect.size.y * (rect.size.y - bottom) / (top - bottom);
                    top = rect.size.y;
                }
            }

            left += rect.position.x;
            right += rect.position.x;
            top += rect.position.y;
            bottom += rect.position.y;

            int n = vertex_data.size();
            index_data.insert(index_data.end(), {
                n + 0, n + 1, n + 2,
                n + 1, n + 3, n + 2,
            });
            vertex_data.push_back({
                {left, top},
                color, {u0, v0}});
            vertex_data.push_back({
                {left, bottom},
                color, {u0, v1}});
            vertex_data.push_back({
                {right, top},
                color, {u1, v0}});
            vertex_data.push_back({
                {right, bottom},
                color, {u1, v1}});
        }
    }
    /*
    sf::Text textElement(sf::String::fromUtf8(std::begin(text), std::end(text)), *font, font_size);
    float y = 0;
    float x = 0;

    //The "base line" of the text draw is the "Y position where the text is drawn" + font_size.
    //The height of normal text is 70% of the font_size.
    //So use those properties to align the text. Depending on the localbounds does not work.
    switch(align)
    {
    case Alignment::TopLeft:
    case Alignment::TopRight:
    case Alignment::TopCenter:
        y = rect.position.y - 0.3 * font_size;
        break;
    case Alignment::BottomLeft:
    case Alignment::BottomRight:
    case Alignment::BottomCenter:
        y = rect.position.y + rect.size.y - font_size;
        break;
    case Alignment::CenterLeft:
    case Alignment::CenterRight:
    case Alignment::Center:
        y = rect.position.y + rect.size.y / 2.0 - font_size + font_size * 0.35;
        break;
    }

    switch(align)
    {
    case Alignment::TopLeft:
    case Alignment::BottomLeft:
    case Alignment::CenterLeft:
        x = rect.position.x - textElement.getLocalBounds().left;
        break;
    case Alignment::TopRight:
    case Alignment::BottomRight:
    case Alignment::CenterRight:
        x = rect.position.x + rect.size.x - textElement.getLocalBounds().width - textElement.getLocalBounds().left;
        break;
    case Alignment::TopCenter:
    case Alignment::BottomCenter:
    case Alignment::Center:
        x = rect.position.x + rect.size.x / 2.0 - textElement.getLocalBounds().width / 2.0 - textElement.getLocalBounds().left;
        break;
    }
    textElement.setPosition(x, y);
    textElement.setColor(sf::Color(color.r, color.g, color.b, color.a));
    target.draw(textElement);
    */
}

void RenderTarget::drawVerticalText(sp::Rect rect, std::string_view text, Alignment align, float font_size, sp::Font* font, glm::u8vec4 color)
{
    if (!font)
        font = default_font;

    /*
    sf::Text textElement(sf::String::fromUtf8(std::begin(text), std::end(text)), *font, font_size);
    textElement.setRotation(-90);
    float x = 0;
    float y = 0;
    x = rect.position.x + rect.size.x / 2.0 - textElement.getLocalBounds().height / 2.0 - textElement.getLocalBounds().top;
    switch(align)
    {
    case Alignment::TopLeft:
    case Alignment::BottomLeft:
    case Alignment::CenterLeft:
        y = rect.position.y + rect.size.y;
        break;
    case Alignment::TopRight:
    case Alignment::BottomRight:
    case Alignment::CenterRight:
        y = rect.position.y + textElement.getLocalBounds().left + textElement.getLocalBounds().width;
        break;
    case Alignment::TopCenter:
    case Alignment::BottomCenter:
    case Alignment::Center:
        y = rect.position.y + rect.size.y / 2.0 + textElement.getLocalBounds().width / 2.0 + textElement.getLocalBounds().left;
        break;
    }
    textElement.setPosition(x, y);
    textElement.setColor(sf::Color(color.r, color.g, color.b, color.a));
    target.draw(textElement);
    */
}

void RenderTarget::drawStretched(sp::Rect rect, std::string_view texture, glm::u8vec4 color)
{
    if (rect.size.x >= rect.size.y)
    {
        drawStretchedH(rect, texture, color);
    }else{
        drawStretchedV(rect, texture, color);
    }
}

void RenderTarget::drawStretchedH(sp::Rect rect, std::string_view texture, glm::u8vec4 color)
{
    sp::Rect uv_rect = getFromAtlas(texture);

    float w = rect.size.y / 2.0f;
    if (w * 2 > rect.size.x)
        w = rect.size.x / 2.0f;
    
    int n = vertex_data.size();
    index_data.insert(index_data.end(), {
        n + 0, n + 1, n + 2,
        n + 1, n + 3, n + 2,
        n + 2, n + 3, n + 4,
        n + 3, n + 5, n + 4,
        n + 4, n + 5, n + 6,
        n + 5, n + 7, n + 6,
    });
    vertex_data.push_back({
        {rect.position.x, rect.position.y},
        color, {uv_rect.position.x, uv_rect.position.y}});
    vertex_data.push_back({
        {rect.position.x, rect.position.y + rect.size.y},
        color, {uv_rect.position.x, uv_rect.position.y + uv_rect.size.y}});
    vertex_data.push_back({
        {rect.position.x + w, rect.position.y},
        color, {uv_rect.position.x + uv_rect.size.x * 0.5f, uv_rect.position.y}});
    vertex_data.push_back({
        {rect.position.x + w, rect.position.y + rect.size.y},
        color, {uv_rect.position.x + uv_rect.size.x * 0.5f, uv_rect.position.y + uv_rect.size.y}});
    vertex_data.push_back({
        {rect.position.x + rect.size.x - w, rect.position.y},
        color, {uv_rect.position.x + uv_rect.size.x * 0.5f, uv_rect.position.y}});
    vertex_data.push_back({
        {rect.position.x + rect.size.x - w, rect.position.y + rect.size.y},
        color, {uv_rect.position.x + uv_rect.size.x * 0.5f, uv_rect.position.y + uv_rect.size.y}});
    vertex_data.push_back({
        {rect.position.x + rect.size.x, rect.position.y},
        color, {uv_rect.position.x + uv_rect.size.x, uv_rect.position.y}});
    vertex_data.push_back({
        {rect.position.x + rect.size.x, rect.position.y + rect.size.y},
        color, {uv_rect.position.x + uv_rect.size.x, uv_rect.position.y + uv_rect.size.y}});
}

void RenderTarget::drawStretchedV(sp::Rect rect, std::string_view texture, glm::u8vec4 color)
{
    sp::Rect uv_rect = getFromAtlas(texture);

    float h = rect.size.x / 2.0;
    if (h * 2 > rect.size.y)
        h = rect.size.y / 2.0f;
    
    int n = vertex_data.size();
    index_data.insert(index_data.end(), {
        n + 0, n + 1, n + 2,
        n + 1, n + 3, n + 2,
        n + 2, n + 3, n + 4,
        n + 3, n + 5, n + 4,
        n + 4, n + 5, n + 6,
        n + 5, n + 7, n + 6,
    });
    vertex_data.push_back({
        {rect.position.x, rect.position.y},
        color, {uv_rect.position.x, uv_rect.position.y}});
    vertex_data.push_back({
        {rect.position.x + rect.size.x, rect.position.y},
        color, {uv_rect.position.x, uv_rect.position.y + uv_rect.size.y}});
    vertex_data.push_back({
        {rect.position.x, rect.position.y + h},
        color, {uv_rect.position.x + uv_rect.size.x * 0.5f, uv_rect.position.y}});
    vertex_data.push_back({
        {rect.position.x + rect.size.x, rect.position.y + h},
        color, {uv_rect.position.x + uv_rect.size.x * 0.5f, uv_rect.position.y + uv_rect.size.y}});
    vertex_data.push_back({
        {rect.position.x, rect.position.y + rect.size.y - h},
        color, {uv_rect.position.x + uv_rect.size.x * 0.5f, uv_rect.position.y}});
    vertex_data.push_back({
        {rect.position.x + rect.size.x, rect.position.y + rect.size.y - h},
        color, {uv_rect.position.x + uv_rect.size.x * 0.5f, uv_rect.position.y + uv_rect.size.y}});
    vertex_data.push_back({
        {rect.position.x, rect.position.y + rect.size.y},
        color, {uv_rect.position.x + uv_rect.size.x, uv_rect.position.y}});
    vertex_data.push_back({
        {rect.position.x + rect.size.x, rect.position.y + rect.size.y},
        color, {uv_rect.position.x + uv_rect.size.x, uv_rect.position.y + uv_rect.size.y}});
}

void RenderTarget::drawStretchedHV(sp::Rect rect, float corner_size, std::string_view texture, glm::u8vec4 color)
{
    sp::Rect uv_rect = getFromAtlas(texture);

    corner_size = std::min(corner_size, rect.size.y / 2.0f);
    corner_size = std::min(corner_size, rect.size.x / 2.0f);

    int n = vertex_data.size();
    index_data.insert(index_data.end(), {
        n + 0, n + 4, n + 1,
        n + 1, n + 4, n + 5,
        n + 1, n + 5, n + 2,
        n + 2, n + 5, n + 6,
        n + 2, n + 6, n + 3,
        n + 3, n + 6, n + 7,

        n + 4, n + 8, n + 5,
        n + 5, n + 8, n + 9,
        n + 5, n + 9, n + 6,
        n + 6, n + 9, n + 10,
        n + 6, n + 10, n + 7,
        n + 7, n + 10, n + 11,

        n + 8, n + 12, n + 9,
        n + 9, n + 12, n + 13,
        n + 9, n + 13, n + 10,
        n + 10, n + 13, n + 14,
        n + 10, n + 14, n + 11,
        n + 11, n + 14, n + 15,
    });
    vertex_data.push_back({
        {rect.position.x, rect.position.y},
        color, {uv_rect.position.x, uv_rect.position.y}});
    vertex_data.push_back({
        {rect.position.x + corner_size, rect.position.y},
        color, {uv_rect.position.x + uv_rect.size.x * 0.5f, uv_rect.position.y}});
    vertex_data.push_back({
        {rect.position.x + rect.size.x - corner_size, rect.position.y},
        color, {uv_rect.position.x + uv_rect.size.x * 0.5f, uv_rect.position.y}});
    vertex_data.push_back({
        {rect.position.x + rect.size.x, rect.position.y},
        color, {uv_rect.position.x + uv_rect.size.x, uv_rect.position.y}});

    vertex_data.push_back({
        {rect.position.x, rect.position.y + corner_size},
        color, {uv_rect.position.x, uv_rect.position.y + uv_rect.size.y * 0.5f}});
    vertex_data.push_back({
        {rect.position.x + corner_size, rect.position.y + corner_size},
        color, {uv_rect.position.x + uv_rect.size.x * 0.5f, uv_rect.position.y + uv_rect.size.y * 0.5f}});
    vertex_data.push_back({
        {rect.position.x + rect.size.x - corner_size, rect.position.y + corner_size},
        color, {uv_rect.position.x + uv_rect.size.x * 0.5f, uv_rect.position.y + uv_rect.size.y * 0.5f}});
    vertex_data.push_back({
        {rect.position.x + rect.size.x, rect.position.y + corner_size},
        color, {uv_rect.position.x + uv_rect.size.x, uv_rect.position.y + uv_rect.size.y * 0.5f}});

    vertex_data.push_back({
        {rect.position.x, rect.position.y + rect.size.y - corner_size},
        color, {uv_rect.position.x, uv_rect.position.y + uv_rect.size.y * 0.5f}});
    vertex_data.push_back({
        {rect.position.x + corner_size, rect.position.y + rect.size.y - corner_size},
        color, {uv_rect.position.x + uv_rect.size.x * 0.5f, uv_rect.position.y + uv_rect.size.y * 0.5f}});
    vertex_data.push_back({
        {rect.position.x + rect.size.x - corner_size, rect.position.y + rect.size.y - corner_size},
        color, {uv_rect.position.x + uv_rect.size.x * 0.5f, uv_rect.position.y + uv_rect.size.y * 0.5f}});
    vertex_data.push_back({
        {rect.position.x + rect.size.x, rect.position.y + rect.size.y - corner_size},
        color, {uv_rect.position.x + uv_rect.size.x, uv_rect.position.y + uv_rect.size.y * 0.5f}});

    vertex_data.push_back({
        {rect.position.x, rect.position.y + rect.size.y},
        color, {uv_rect.position.x, uv_rect.position.y + uv_rect.size.y}});
    vertex_data.push_back({
        {rect.position.x + corner_size, rect.position.y + rect.size.y},
        color, {uv_rect.position.x + uv_rect.size.x * 0.5f, uv_rect.position.y + uv_rect.size.y}});
    vertex_data.push_back({
        {rect.position.x + rect.size.x - corner_size, rect.position.y + rect.size.y},
        color, {uv_rect.position.x + uv_rect.size.x * 0.5f, uv_rect.position.y + uv_rect.size.y}});
    vertex_data.push_back({
        {rect.position.x + rect.size.x, rect.position.y + rect.size.y},
        color, {uv_rect.position.x + uv_rect.size.x, uv_rect.position.y + uv_rect.size.y}});
}

void RenderTarget::finish()
{
    if (index_data.size())
    {
        shader->bind();
        glBindBuffer(GL_ARRAY_BUFFER, vertices_vbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices_vbo);

        glUniform1i(shader->getUniformLocation("u_texture"), 0);
        glActiveTexture(GL_TEXTURE0);
        atlas_texture->bind();

        glBindBuffer(GL_ARRAY_BUFFER, vertices_vbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(VertexData) * vertex_data.size(), vertex_data.data(), GL_DYNAMIC_DRAW);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint16_t) * index_data.size(), index_data.data(), GL_DYNAMIC_DRAW);

        glVertexAttribPointer(shader->getAttributeLocation("a_position"), 2, GL_FLOAT, GL_FALSE, sizeof(VertexData), (void*)0);
        glEnableVertexAttribArray(shader->getAttributeLocation("a_position"));
        glVertexAttribPointer(shader->getAttributeLocation("a_color"), 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(VertexData), (void*)offsetof(VertexData, color));
        glEnableVertexAttribArray(shader->getAttributeLocation("a_color"));
        glVertexAttribPointer(shader->getAttributeLocation("a_texcoords"), 2, GL_FLOAT, GL_FALSE, sizeof(VertexData), (void*)offsetof(VertexData, uv));
        glEnableVertexAttribArray(shader->getAttributeLocation("a_texcoords"));

        glDrawElements(GL_TRIANGLES, index_data.size(), GL_UNSIGNED_SHORT, nullptr);

        vertex_data.clear();
        index_data.clear();
    }
}

glm::vec2 RenderTarget::getVirtualSize()
{
    return virtual_size;
}

glm::ivec2 RenderTarget::getPhysicalSize()
{
    return physical_size;
}

glm::ivec2 RenderTarget::virtualToPixelPosition(glm::vec2)
{
    return {1,1};
}

}
