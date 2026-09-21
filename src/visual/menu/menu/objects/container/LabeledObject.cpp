/*
  Created on 07.07.18.
*/

#include <menu/object/container/LabeledObject.hpp>
#include <menu/Menu.hpp>
#include <menu/Utility.hpp>

void zerokernel::LabeledObject::loadFromXml(const tinyxml2::XMLElement *data)
{
    Container::loadFromXml(data);

    const char *label_text;
    if (tinyxml2::XML_SUCCESS == data->QueryStringAttribute("label", &label_text))
    {
        setLabel(label_text);
    }
}

zerokernel::LabeledObject::LabeledObject() : BaseMenuObject{}
{
    bb.width.setContent();
    bb.height.setContent();
}

void zerokernel::LabeledObject::createLabel()
{
    auto label = std::make_unique<Text>();
    label->setParent(this);
    this->label = label.get();
    label->bb.setMargin(0, 0, 6, 0);
    label->bb.width.setContent();
    label->bb.height.setFill();
    addObject(std::move(label));
}

void zerokernel::LabeledObject::setLabel(std::string text)
{
    if (label == nullptr)
    {
        createLabel();
    }
    full_label = text;
    label->set(std::move(text));
}

void zerokernel::LabeledObject::reorderElements()
{
    BaseMenuObject *control = nullptr;
    for (auto &o : objects)
    {
        if (o.get() != label)
        {
            control = o.get();
            break;
        }
    }

    const int box_w = bb.getContentBox().width;
    const bool fill = bb.width.mode == BoundingBox::SizeMode::Mode::FILL;

    if (control)
    {
        const int control_w = control->getBoundingBox().getFullBox().width;
        int x               = box_w - control_w;
        if (label)
        {
            const int gap    = label->getBoundingBox().margin.right + control->getBoundingBox().margin.left;
            const int min_x  = label->getBoundingBox().getFullBox().width + gap;
            if (!fill && x < min_x)
                x = min_x;
            if (fill)
            {
                if (x < 0)
                    x = 0;
                const int label_space = x - gap;
                if (label->font && !full_label.empty() && label_space >= 0)
                    label->set(utility::dotCompactString(full_label, *label->font, label_space, false));
            }
        }
        else if (x < 0)
            x = 0;
        control->move(x, 0);
    }
    if (label)
        label->move(0, 0);
}

void zerokernel::LabeledObject::setObject(std::unique_ptr<zerokernel::BaseMenuObject> &&object)
{
    addObject(std::move(object));
}
