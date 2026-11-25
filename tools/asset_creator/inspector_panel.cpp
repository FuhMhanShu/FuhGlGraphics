#include "inspector_panel.hpp"

#include "commands.hpp"

#include <fuhglgraphics/mcp/tool_catalog.hpp>
#include <fuhglgraphics/render/effects.hpp>
#include <fuhglgraphics/scene/component.hpp>
#include <fuhglgraphics/scene/node.hpp>
#include <fuhglgraphics/water/water_surface.hpp>

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QVBoxLayout>

#include <cstring>

namespace assetcreator {

using namespace fuhglgraphics;

namespace {

QWidget* vec3Widget(QWidget* parent, const fuhgl::math::Vec3& v, QDoubleSpinBox* boxes[3]) {
    auto* w = new QWidget(parent);
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    for (int i = 0; i < 3; ++i) {
        boxes[i] = new QDoubleSpinBox(w);
        boxes[i]->setRange(-10000.0, 10000.0);
        boxes[i]->setDecimals(3);
        boxes[i]->setSingleStep(0.1);
        boxes[i]->setValue(i == 0 ? v.x : (i == 1 ? v.y : v.z));
        h->addWidget(boxes[i]);
    }
    return w;
}

} // namespace

InspectorPanel::InspectorPanel(AssetDocument* doc, QWidget* parent)
    : QWidget(parent), doc_(doc) {
    auto* outer = new QVBoxLayout(this);

    auto* form = new QFormLayout();
    nameEdit_ = new QLineEdit(this);
    form->addRow(tr("Name"), nameEdit_);
    outer->addLayout(form);

    auto* trsForm = new QFormLayout();
    outer->addLayout(trsForm);
    for (int i = 0; i < 2; ++i) {
        QWidget* w = vec3Widget(this, fuhgl::math::Vec3{0.f, 0.f, 0.f}, trsBoxes_[i]);
        trsForm->addRow(i == 0 ? tr("Translation") : tr("Scale"), w);
    }
    {   // rotation as quat xyzw (FuhGLCore has fromEuler but no toEuler)
        auto* w = new QWidget(this);
        auto* h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        for (int i = 0; i < 4; ++i) {
            quatBoxes_[i] = new QDoubleSpinBox(w);
            quatBoxes_[i]->setRange(-1.0, 1.0);
            quatBoxes_[i]->setDecimals(4);
            quatBoxes_[i]->setSingleStep(0.01);
            h->addWidget(quatBoxes_[i]);
        }
        trsForm->addRow(tr("Rotation (quat xyzw)"), w);
    }
    // wire TRS rows once: editingFinished -> SetTrs command
    auto onTrsEdited = [this]() {
        if (updating_ || !node_ || !pushCommand) return;
        scene::Node* n = node_;
        fuhgl::math::Vec3 newT{static_cast<float>(trsBoxes_[0][0]->value()),
                               static_cast<float>(trsBoxes_[0][1]->value()),
                               static_cast<float>(trsBoxes_[0][2]->value())};
        fuhgl::math::Vec3 newS{static_cast<float>(trsBoxes_[1][0]->value()),
                               static_cast<float>(trsBoxes_[1][1]->value()),
                               static_cast<float>(trsBoxes_[1][2]->value())};
        fuhgl::math::Quat newR{static_cast<float>(quatBoxes_[0]->value()),
                               static_cast<float>(quatBoxes_[1]->value()),
                               static_cast<float>(quatBoxes_[2]->value()),
                               static_cast<float>(quatBoxes_[3]->value())};
        pushCommand(makeSetTrsCommand(doc_, n, newT, newR, newS));
    };
    for (int i = 0; i < 2; ++i)
        for (int k = 0; k < 3; ++k)
            connect(trsBoxes_[i][k], &QDoubleSpinBox::editingFinished, this, onTrsEdited);
    for (int k = 0; k < 4; ++k)
        connect(quatBoxes_[k], &QDoubleSpinBox::editingFinished, this, onTrsEdited);

    outer->addWidget(new QLabel(tr("Effects (deferred renderer)"), this));
    effectsList_ = new QListWidget(this);
    effectsList_->setMaximumHeight(220);
    outer->addWidget(effectsList_);

    outer->addWidget(new QLabel(tr("Renderer parameters"), this));
    paramsLayout_ = new QVBoxLayout();
    outer->addLayout(paramsLayout_);

    waterBox_ = new QWidget(this);
    waterBox_->setLayout(new QVBoxLayout());
    outer->addWidget(waterBox_);
    outer->addStretch(1);

    connect(nameEdit_, &QLineEdit::editingFinished, this, [this]() {
        if (updating_ || !node_ || !pushCommand) return;
        scene::Node* n = node_;
        const std::string newName = nameEdit_->text().toStdString();
        const std::string oldName = n->name();
        if (newName == oldName) return;
        pushCommand(std::make_unique<LambdaCommand>(
            doc_, tr("Rename"), [n, newName]() { n->setName(newName); },
            [n, oldName]() { n->setName(oldName); }));
    });
    connect(effectsList_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (updating_ || !node_ || !pushCommand) return;
        const int idx = item->data(Qt::UserRole).toInt();
        const bool on = item->checkState() == Qt::Checked;
        pushCommand(makeSetEffectCommand(doc_, node_, static_cast<render::Effect>(idx), on));
    });
    connect(doc_, &AssetDocument::changed, this, [this]() { rebuild(); });
}

void InspectorPanel::setNode(scene::Node* node) {
    node_ = node;
    rebuild();
}

void InspectorPanel::rebuild() {
    updating_ = true;
    if (!node_) {
        nameEdit_->clear();
        effectsList_->clear();
        waterBox_->setVisible(false);
        updating_ = false;
        return;
    }
    scene::Node* n = node_;
    nameEdit_->setText(QString::fromStdString(n->name()));
    const fuhgl::math::Vec3 t = n->translation, sc = n->scale;
    const fuhgl::math::Quat q = n->rotation;
    for (int k = 0; k < 3; ++k) {
        trsBoxes_[0][k]->setValue(k == 0 ? t.x : (k == 1 ? t.y : t.z));
        trsBoxes_[1][k]->setValue(k == 0 ? sc.x : (k == 1 ? sc.y : sc.z));
    }
    quatBoxes_[0]->setValue(q.x);
    quatBoxes_[1]->setValue(q.y);
    quatBoxes_[2]->setValue(q.z);
    quatBoxes_[3]->setValue(q.w);

    effectsList_->clear();
    for (int i = 0; i < render::kEffectCount; ++i) {
        const render::Effect e = static_cast<render::Effect>(i);
        auto* item = new QListWidgetItem(QString::fromLatin1(render::effectName(e)));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(n->effects.enabled(e) ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, i);
        effectsList_->addItem(item);
    }

    while (paramsLayout_->count()) {
        QLayoutItem* it = paramsLayout_->takeAt(0);
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    auto* form = new QFormLayout();
    paramsLayout_->addLayout(form);
    auto pushParam = [this, n](const QString& name, double value) {
        io::Json v;
        v.type = io::Json::Type::Number;
        v.number = value;
        if (pushCommand) pushCommand(makeSetEffectParamCommand(doc_, n, name, v));
    };
    auto addRow = [form, this](const QString& label, double value, double min, double max, double step,
                               std::function<void(double)> onFinished) {
        auto* sb = new QDoubleSpinBox(this);
        sb->setRange(min, max);
        sb->setDecimals(4);
        sb->setSingleStep(step);
        sb->setValue(value);
        form->addRow(label, sb);
        connect(sb, &QDoubleSpinBox::editingFinished, this, [onFinished, sb]() { onFinished(sb->value()); });
    };
    const scene::EffectParams& p = n->effects.params;
    addRow(tr("exposure"), p.exposure, 0.01, 10, 0.05, [pushParam](double v) { pushParam("exposure", v); });
    addRow(tr("bloom threshold"), p.bloomThreshold, 0, 16, 0.1, [pushParam](double v) { pushParam("bloomThreshold", v); });
    addRow(tr("bloom strength"), p.bloomStrength, 0, 4, 0.05, [pushParam](double v) { pushParam("bloomStrength", v); });
    addRow(tr("SSR strength"), p.ssrStrength, 0, 2, 0.05, [pushParam](double v) { pushParam("ssrStrength", v); });
    addRow(tr("SSAO radius"), p.ssaoRadius, 0.05, 10, 0.05, [pushParam](double v) { pushParam("ssaoRadius", v); });
    addRow(tr("fog density"), p.volumetricDensity, 0, 0.2, 0.005, [pushParam](double v) { pushParam("volumetricDensity", v); });
    addRow(tr("wetness"), p.wetnessAmount, 0, 1, 0.05, [pushParam](double v) { pushParam("wetnessAmount", v); });
    addRow(tr("DoF focus"), p.dofFocusDist, 0.1, 200, 0.5, [pushParam](double v) { pushParam("dofFocusDist", v); });
    addRow(tr("render scale"), p.renderScale, 0.25, 1, 0.05, [pushParam](double v) { pushParam("renderScale", v); });

    // G13 water section when the node owns a WaterComponent
    auto* wl = static_cast<QVBoxLayout*>(waterBox_->layout());
    while (wl->count()) {
        QLayoutItem* it = wl->takeAt(0);
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    auto* wcomp = n->find<scene::WaterComponent>();
    if (wcomp) {
        waterBox_->setVisible(true);
        wl->addWidget(new QLabel(tr("Water (G13)"), waterBox_));
        auto* wf = new QFormLayout();
        wl->addLayout(wf);
        water::WaterSurface* surf = &wcomp->surface;
        auto pushWater = [this, surf](const char* key, double newValue) {
            io::Json oldV = io::Json{};
            oldV.type = io::Json::Type::Number;
            if (std::strcmp(key, "foamStrength") == 0) oldV.number = surf->foamStrength;
            else if (std::strcmp(key, "reflectionStrength") == 0) oldV.number = surf->reflectionStrength;
            else if (std::strcmp(key, "time") == 0) oldV.number = surf->time;
            io::Json newJ = oldV;
            newJ.number = newValue;
            auto apply = [surf, key](const io::Json& val) {
                mcp::ToolHost h;
                h.water = surf;
                io::Json a;
                a.type = io::Json::Type::Object;
                a.obj[key] = val;
                std::string err;
                mcp::callTool("water_set_params", a, h, &err);
            };
            if (pushCommand)
                pushCommand(std::make_unique<LambdaCommand>(
                    doc_, tr("Water %1").arg(key), [apply, newJ]() { apply(newJ); },
                    [apply, oldV]() { apply(oldV); }));
        };
        auto addWaterRow = [wf, this, pushWater](const QString& label, const char* key, double value,
                                                 double min, double max, double step) {
            auto* sb = new QDoubleSpinBox(this);
            sb->setRange(min, max);
            sb->setDecimals(4);
            sb->setSingleStep(step);
            sb->setValue(value);
            wf->addRow(label, sb);
            connect(sb, &QDoubleSpinBox::editingFinished, this,
                    [pushWater, key, sb]() { pushWater(key, sb->value()); });
        };
        addWaterRow(tr("foam strength"), "foamStrength", surf->foamStrength, 0, 2, 0.05);
        addWaterRow(tr("reflection"), "reflectionStrength", surf->reflectionStrength, 0, 2, 0.05);
        addWaterRow(tr("time"), "time", surf->time, 0, 10000, 0.1);
    } else {
        waterBox_->setVisible(false);
    }
    updating_ = false;
}

} // namespace assetcreator
