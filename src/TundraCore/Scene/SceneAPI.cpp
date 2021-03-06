// For conditions of distribution and use, see copyright notice in LICENSE

#include "StableHeaders.h"

#include "SceneAPI.h"
#include "Scene/Scene.h"
#include "IComponentFactory.h"
#include "IComponent.h"
#include "IRenderer.h"
#include "AssetReference.h"
#include "EntityReference.h"
#include "Framework.h"
#include "Math/Transform.h"
#include "Math/Color.h"
#include "Math/Point.h"
#include "DynamicComponent.h"
#include "Name.h"
#include "PlaceholderComponent.h"
#include "LoggingFunctions.h"
#include "Script.h"

#include <Math/Quat.h>
#include <Math/float2.h>
#include <Math/float3.h>
#include <Math/float4.h>

#include <Urho3D/Resource/XMLElement.h>

namespace Tundra
{

StringVector SceneAPI::attributeTypeNames;

SceneAPI::SceneAPI(Framework *owner) :
    Object(owner->GetContext()),
    framework(owner)
{
    attributeTypeNames.Clear();
    attributeTypeNames.Push(IAttribute::StringTypeName);
    attributeTypeNames.Push(IAttribute::IntTypeName);
    attributeTypeNames.Push(IAttribute::RealTypeName);
    attributeTypeNames.Push(IAttribute::ColorTypeName);
    attributeTypeNames.Push(IAttribute::Float2TypeName);
    attributeTypeNames.Push(IAttribute::Float3TypeName);
    attributeTypeNames.Push(IAttribute::Float4TypeName);
    attributeTypeNames.Push(IAttribute::BoolTypeName);
    attributeTypeNames.Push(IAttribute::UIntTypeName);
    attributeTypeNames.Push(IAttribute::QuatTypeName);
    attributeTypeNames.Push(IAttribute::AssetReferenceTypeName);
    attributeTypeNames.Push(IAttribute::AssetReferenceListTypeName);
    attributeTypeNames.Push(IAttribute::EntityReferenceTypeName);
    attributeTypeNames.Push(IAttribute::VariantTypeName);
    attributeTypeNames.Push(IAttribute::VariantListTypeName);
    attributeTypeNames.Push(IAttribute::TransformTypeName);
    attributeTypeNames.Push(IAttribute::PointTypeName);
    assert(attributeTypeNames.Size() == IAttribute::NumTypes - 1 && "Attribute type registration mismatch!"); // -1 as IAttributeNoneTypeName is not in the list.

    // Name, DynamicComponent & Script are always available
    RegisterComponentFactory(ComponentFactoryPtr(new GenericComponentFactory<Name>()));
    RegisterComponentFactory(ComponentFactoryPtr(new GenericComponentFactory<DynamicComponent>()));
    RegisterComponentFactory(ComponentFactoryPtr(new GenericComponentFactory<Script>()));
}

SceneAPI::~SceneAPI()
{
    Reset();
}

void SceneAPI::Reset()
{
    while (scenes.Size())
        RemoveScene(scenes.Begin()->first_);

    componentFactories.Clear();
    componentFactoriesByTypeid.Clear();
}

ScenePtr SceneAPI::SceneByName(const String &name) const
{
    SceneMap::ConstIterator scene = scenes.Find(name);
    if (scene != scenes.End())
        return scene->second_;
    return ScenePtr();
}

Scene *SceneAPI::MainCameraScene()
{
    if (!framework || !framework->Renderer())
        return nullptr;
    return framework->Renderer()->MainCameraScene();

}

ScenePtr SceneAPI::CreateScene(const String &name, bool viewEnabled, bool authority, AttributeChange::Type change)
{
    if (SceneByName(name))
        return ScenePtr();

    ScenePtr newScene(new Scene(name, framework, viewEnabled, authority));
    scenes[name] = newScene;

    // Emit signal of creation
    if (change != AttributeChange::Disconnected)
    {
        SceneCreated.Emit(newScene.Get(), change);
    }

    return newScene;
}

bool SceneAPI::RemoveScene(const String &name, AttributeChange::Type change)
{
    SceneMap::Iterator sceneIter = scenes.Find(name);
    if (sceneIter == scenes.End())
        return false;

    // Remove entities before the scene subsystems or worlds are erased by various modules
    sceneIter->second_->RemoveAllEntities(false, change);

    // Emit signal about removed scene
    if (change != AttributeChange::Disconnected)
    {
        SceneAboutToBeRemoved.Emit(sceneIter->second_.Get(), change);
    }

    scenes.Erase(sceneIter);
    return true;
}

const SceneMap &SceneAPI::Scenes() const
{
    return scenes;
}

SceneMap &SceneAPI::Scenes()
{
    return scenes;
}

bool SceneAPI::IsComponentFactoryRegistered(const String &typeName) const
{
    return componentFactories.Find(IComponent::EnsureTypeNameWithoutPrefix(typeName)) != componentFactories.End();
}

bool SceneAPI::IsPlaceholderComponentRegistered(const String &typeName) const
{
    return placeholderComponentTypeIds.Find(IComponent::EnsureTypeNameWithoutPrefix(typeName)) != placeholderComponentTypeIds.End();
}

bool SceneAPI::IsComponentTypeRegistered(const String& typeName) const
{
    String nameWithoutPrefix = IComponent::EnsureTypeNameWithoutPrefix(typeName);
    return componentFactories.Find(nameWithoutPrefix) != componentFactories.End() ||
        placeholderComponentTypeIds.Find(nameWithoutPrefix) != placeholderComponentTypeIds.End();
}

void SceneAPI::RegisterComponentFactory(const ComponentFactoryPtr &factory)
{
    if (factory->TypeName().Trimmed() != factory->TypeName() || factory->TypeName().Empty() || factory->TypeId() == 0)
    {
        LogError("Cannot add a new ComponentFactory for component type name \"" + factory->TypeName() +
            "\" and type ID " + String(factory->TypeId()) + ". Invalid input!");
        return;
    }

    ComponentFactoryMap::Iterator existing = componentFactories.Find(factory->TypeName());
    ComponentFactoryWeakMap::Iterator existing2 = componentFactoriesByTypeid.Find(factory->TypeId());
    ComponentFactoryPtr existingFactory;
    if (existing != componentFactories.End())
        existingFactory = existing->second_;
    if (!existingFactory && existing2 != componentFactoriesByTypeid.End())
        existingFactory = existing2->second_.Lock();

    if (existingFactory)
    {
        LogError("Cannot add a new ComponentFactory for component type name \"" + factory->TypeName() + "\" and type ID " + 
            String(factory->TypeId()) + ". Conflicting type factory with type name " + existingFactory->TypeName() + 
            " and type ID " + String(existingFactory->TypeId()) + " already exists!");
        return;
    }

    componentFactories[factory->TypeName()] = factory;
    componentFactoriesByTypeid[factory->TypeId()] = factory;
}

ComponentPtr SceneAPI::CreateComponentByName(Scene* scene, const String &componentTypename, const String &newComponentName) const
{
    ComponentFactoryPtr factory = GetFactory(componentTypename);
    if (!factory)
    {
        // If no actual factory, try creating a placeholder component
        PlaceholderComponentTypeIdMap::ConstIterator i = placeholderComponentTypeIds.Find(IComponent::EnsureTypeNameWithoutPrefix(componentTypename));
        if (i != placeholderComponentTypeIds.End())
            return CreatePlaceholderComponentById(scene, i->second_, newComponentName);

        LogError("Cannot create component for type \"" + componentTypename + "\" - no factory exists!");
        return ComponentPtr();
    }
    return factory->Create(context_, scene, newComponentName);
}

ComponentPtr SceneAPI::CreateComponentById(Scene* scene, u32 componentTypeid, const String &newComponentName) const
{
    ComponentFactoryPtr factory = GetFactory(componentTypeid);
    if (!factory)
    {
        // If no actual factory, try creating a placeholder component
        PlaceholderComponentTypeMap::ConstIterator i = placeholderComponentTypes.Find(componentTypeid);
        if (i != placeholderComponentTypes.End())
            return CreatePlaceholderComponentById(scene, componentTypeid, newComponentName);

        LogError("Cannot create component for type ID \"" + String(componentTypeid) + "\" - no factory exists!");
        return ComponentPtr();
    }
    return factory->Create(context_, scene, newComponentName);
}

String SceneAPI::ComponentTypeNameForTypeId(u32 componentTypeid) const
{
    ComponentFactoryPtr factory = GetFactory(componentTypeid);
    if (factory)
        return factory->TypeName();
    else
    {
        // Check also placeholder types
        PlaceholderComponentTypeMap::ConstIterator i = placeholderComponentTypes.Find(componentTypeid);
        if (i != placeholderComponentTypes.End())
            return i->second_.typeName;
        else
            return "";
    }
}

u32 SceneAPI::ComponentTypeIdForTypeName(const String &componentTypename) const
{
    ComponentFactoryPtr factory = GetFactory(componentTypename);
    if (factory)
        return factory->TypeId();
    else
    {
        // Check also placeholder types
        PlaceholderComponentTypeIdMap::ConstIterator i = placeholderComponentTypeIds.Find(IComponent::EnsureTypeNameWithoutPrefix(componentTypename));
        if (i != placeholderComponentTypeIds.End())
            return i->second_;
        else
            return 0;
    }
}

String SceneAPI::AttributeTypeNameForTypeId(u32 attributeTypeid)
{
    attributeTypeid--; // Skip 0 which is illegal
    if (attributeTypeid < (u32)attributeTypeNames.Size())
        return attributeTypeNames[attributeTypeid];
    else
        return "";
}

u32 SceneAPI::AttributeTypeIdForTypeName(const String &attributeTypename)
{
    for (u32 i = 0; i < attributeTypeNames.Size(); ++i)
    {
        if (attributeTypeNames[i].Compare(attributeTypename, false) == 0 ||
            attributeTypeNames[i].Compare(attributeTypename.Substring(1), false) == 0) // Handle Q-prefixed deprecated forms
        {
            return i + 1; // 0 is illegal, actual types start from 1
        }
    }
    return 0;
}

/*
ComponentPtr SceneAPI::CloneComponent(const ComponentPtr &component, const String &newComponentName)
{
    ComponentFactoryPtr factory = GetFactory(component->TypeId());
    if (!factory)
        return ComponentPtr();

    return factory->Clone(component.get(), newComponentName);
}
*/

IAttribute *SceneAPI::CreateAttribute(const String &attributeTypeName, const String &newAttributeId)
{
    IAttribute *attr = CreateAttribute(AttributeTypeIdForTypeName(attributeTypeName), newAttributeId);
    // CreateAttribute(u32) already logs error, but AttributeTypeIdForTypeName returns 0 for
    // invalid type names and hence we have no idea what the user has inputted here so log the type name.
    if (!attr)
        LogError("Erroneous attribute type name \"" + attributeTypeName + "\".");
    return attr;
}

IAttribute* SceneAPI::CreateAttribute(u32 attributeTypeId, const String& newAttributeId)
{
    IAttribute *attribute = 0;
    switch(attributeTypeId)
    {
    case IAttribute::StringId:
        attribute = new Attribute<String>(0, newAttributeId.CString()); break;
    case IAttribute::IntId:
        attribute = new Attribute<int>(0, newAttributeId.CString()); break;
    case IAttribute::RealId:
        attribute = new Attribute<float>(0, newAttributeId.CString()); break;
    case IAttribute::ColorId:
        attribute = new Attribute<Color>(0, newAttributeId.CString()); break;
    case IAttribute::Float2Id:
        attribute = new Attribute<float2>(0, newAttributeId.CString()); break;
    case IAttribute::Float3Id:
        attribute = new Attribute<float3>(0, newAttributeId.CString()); break;
    case IAttribute::Float4Id:
        attribute = new Attribute<float4>(0, newAttributeId.CString()); break;
    case IAttribute::BoolId:
        attribute = new Attribute<bool>(0, newAttributeId.CString()); break;
    case IAttribute::UIntId:
        attribute = new Attribute<uint>(0, newAttributeId.CString()); break;
    case IAttribute::QuatId:
        attribute = new Attribute<Quat>(0, newAttributeId.CString()); break;
    case IAttribute::AssetReferenceId:
        attribute = new Attribute<AssetReference>(0, newAttributeId.CString());break;
    case IAttribute::AssetReferenceListId:
        attribute = new Attribute<AssetReferenceList>(0, newAttributeId.CString());break;
    case IAttribute::EntityReferenceId:
        attribute = new Attribute<EntityReference>(0, newAttributeId.CString());break;
    case IAttribute::VariantId:
        attribute = new Attribute<Variant>(0, newAttributeId.CString()); break;
    case IAttribute::VariantListId:
        attribute = new Attribute<VariantList>(0, newAttributeId.CString()); break;
    case IAttribute::TransformId:
        attribute = new Attribute<Transform>(0, newAttributeId.CString()); break;
    case IAttribute::PointId:
        attribute = new Attribute<Point>(0, newAttributeId.CString()); break;
    default:
        LogError("SceneAPI::CreateAttribute: unknown attribute type ID \"" + String(attributeTypeId) + "\" when creating attribute \"" + newAttributeId + "\")!");
        break;
    }

    if (attribute)
        attribute->dynamic = true;
    return attribute;
}

const StringVector &SceneAPI::AttributeTypes()
{
    return attributeTypeNames;
}

void SceneAPI::RegisterPlaceholderComponentType(Urho3D::XMLElement& element, AttributeChange::Type change)
{
    ComponentDesc desc;
    if (!element.HasAttribute("type"))
    {
        LogError("Component XML element is missing type attribute, can not register placeholder component type");
        return;
    }

    desc.typeId = element.GetUInt("typeId");
    desc.typeName = IComponent::EnsureTypeNameWithoutPrefix(element.GetAttribute("type"));
    desc.name = element.GetAttribute("name");

    Urho3D::XMLElement child = element.GetChild("attribute");
    while (child)
    {
        AttributeDesc attr;
        attr.id = child.GetAttribute("id");
        // Fallback if ID is not defined
        if (attr.id.Empty())
            attr.id = child.GetAttribute("name");
        attr.name = child.GetAttribute("name");
        attr.typeName = child.GetAttribute("type");
        attr.value = child.GetAttribute("value");
        
        // Older scene content does not have attribute typenames, these can not be used
        if (!attr.typeName.Empty())
            desc.attributes.Push(attr);
        else
            LogWarning("Can not store placeholder component attribute " + attr.name + ", no type specified");

        child = child.GetNext("attribute");
    }

    RegisterPlaceholderComponentType(desc, change);
}

void SceneAPI::RegisterPlaceholderComponentType(ComponentDesc desc, AttributeChange::Type change)
{
    // If no typeid defined, generate from the name without prefix
    // (eg. if script is registering a type, do not require it to invent a typeID)
    if (desc.typeId == 0 || desc.typeId == 0xffffffff)
        desc.typeId = (StringHash(IComponent::EnsureTypeNameWithoutPrefix(desc.typeName)).Value() & 0xffff) | 0x10000;

    desc.typeName = IComponent::EnsureTypeNameWithoutPrefix(desc.typeName);

    if (GetFactory(desc.typeId))
    {
        LogError("Component factory for component typeId " + String(desc.typeId) + " already exists, can not register placeholder component type");
        return;
    }
    if (desc.typeName.Empty())
    {
        LogError("Empty typeName in placeholder component description, can not register");
        return;
    }

    if (placeholderComponentTypes.Find(desc.typeId) == placeholderComponentTypes.End())
        LogInfo("Registering placeholder component type " + desc.typeName);
    else
    {
        // Check for hash collision
        /// \todo Is not yet resolved in any meaningful way, the old desc is still overwritten
        if (placeholderComponentTypes[desc.typeId].typeName != desc.typeName)
            LogError("Placeholder component typeId hash collision! Old name " + placeholderComponentTypes[desc.typeId].typeName + " new name " + desc.typeName);
        else
            LogWarning("Re-registering placeholder component type " + desc.typeName);
    }

    placeholderComponentTypes[desc.typeId] = desc;
    placeholderComponentTypeIds[desc.typeName] = desc.typeId;


    PlaceholderComponentTypeRegistered.Emit(desc.typeId, desc.typeName, change);
}

void SceneAPI::RegisterComponentType(const String& typeName, IComponent* component)
{
    if (!component)
        return;

    ComponentDesc desc;
    desc.typeName = typeName;
    desc.typeId = 0xffffffff; // Calculate from hash in RegisterPlaceholderComponentType()
    const AttributeVector& attrs = component->Attributes();
    for (uint i = 0; i < attrs.Size(); ++i)
    {
        IAttribute* attr = attrs[i];
        if (!attr)
            continue;
        AttributeDesc attrDesc;
        attrDesc.id = attr->Id();
        attrDesc.name = attr->Name();
        attrDesc.typeName = attr->TypeName();
        desc.attributes.Push(attrDesc);
    }

    RegisterPlaceholderComponentType(desc);
}

ComponentPtr SceneAPI::CreatePlaceholderComponentById(Scene* scene, u32 componentTypeid, const String &newComponentName) const
{
    PlaceholderComponentTypeMap::ConstIterator i = placeholderComponentTypes.Find(componentTypeid);
    if (i == placeholderComponentTypes.End())
    {
        LogError("Unknown placeholder component type " + String(componentTypeid) + ", can not create placeholder component");
        return ComponentPtr();
    }

    const ComponentDesc& desc = i->second_;

    PlaceholderComponent* component = new PlaceholderComponent(context_, scene);
    component->SetTypeId(componentTypeid);
    component->SetTypeName(desc.typeName);
    component->SetName(newComponentName);

    for (u32 j = 0; j < desc.attributes.Size(); ++j)
    {
        const AttributeDesc& attr = desc.attributes[j];
        component->CreateAttribute(attr.typeName, attr.id, attr.name);
    }

    return ComponentPtr(component);
}

StringVector SceneAPI::ComponentTypes() const
{
    StringVector componentTypes;
    for(ComponentFactoryMap::ConstIterator iter = componentFactories.Begin(); iter != componentFactories.End(); ++iter)
        componentTypes.Push(iter->first_);
    return componentTypes;
}

ComponentFactoryPtr SceneAPI::GetFactory(const String &typeName) const
{
    ComponentFactoryMap::ConstIterator factory = componentFactories.Find(IComponent::EnsureTypeNameWithoutPrefix(typeName));
    if (factory == componentFactories.End())
        return ComponentFactoryPtr();
    else
        return factory->second_;
}

ComponentFactoryPtr SceneAPI::GetFactory(u32 typeId) const
{
    ComponentFactoryWeakMap::ConstIterator factory = componentFactoriesByTypeid.Find(typeId);
    if (factory == componentFactoriesByTypeid.End())
        return ComponentFactoryPtr();
    else
        return factory->second_.Lock();
}

}
