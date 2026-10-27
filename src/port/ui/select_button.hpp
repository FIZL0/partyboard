// Credits: TwilitRealm

#pragma once

#include "component.hpp"
#include "ui.hpp"

#include <functional>
#include <utility>

namespace partyboard::ui {

using SelectButtonCallback = std::function<void()>;

class SelectButton : public FluentComponent<SelectButton> {
public:
    struct Props {
        Rml::String key;
        Rml::String value;
        Rml::String icon;
        bool modified = false;
        bool submit = true;
    };

    SelectButton(Rml::Element *parent, Props props);

    virtual bool modified() const;
    void set_modified(bool value);
    void set_value_label(const Rml::String &value);
    // N59: an image after the value (a button glyph's source, port/ui/button_glyphs.hpp); empty hides it
    void set_glyph(const Rml::String &src);
    SelectButton &on_pressed(SelectButtonCallback callback);

protected:
    void update_props(Props props);
    virtual bool handle_nav_command(NavCommand cmd);

    Props mProps;
    Rml::Element *mKeyElem = nullptr;
    Rml::Element *mIconElem = nullptr;
    Rml::Element *mValueElem = nullptr;
    Rml::Element *mGlyphElem = nullptr;
    Rml::String mGlyph;
};

class BaseControlledSelectButton : public SelectButton {
public:
    BaseControlledSelectButton(Rml::Element *parent, Props props)
        : SelectButton(parent, std::move(props))
    {
    }

    void update() override;

protected:
    virtual Rml::String format_value() = 0;
    virtual Rml::String format_glyph() { return {}; }
};

class ControlledSelectButton : public BaseControlledSelectButton {
public:
    struct Props {
        Rml::String key;
        std::function<Rml::String()> getValue;
        std::function<bool()> isDisabled;
        std::function<bool()> isModified;
        bool submit = true;
        // N59: the glyph image source shown after the value (button_glyph_src)
        std::function<Rml::String()> getGlyph;
    };

    ControlledSelectButton(Rml::Element *parent, Props props)
        : BaseControlledSelectButton(parent,
              {
                  .key = std::move(props.key),
                  .submit = props.submit,
              })
        , mGetValue(std::move(props.getValue))
        , mIsDisabled(std::move(props.isDisabled))
        , mIsModified(std::move(props.isModified))
        , mGetGlyph(std::move(props.getGlyph))
    {
    }

    bool modified() const override;
    bool disabled() const override;

protected:
    Rml::String format_value() override;
    Rml::String format_glyph() override;

    std::function<Rml::String()> mGetValue;
    std::function<bool()> mIsDisabled;
    std::function<bool()> mIsModified;
    std::function<Rml::String()> mGetGlyph;
};

} // namespace partyboard::ui
