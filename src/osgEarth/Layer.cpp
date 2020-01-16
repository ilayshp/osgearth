/* -*-c++-*- */
/* osgEarth - Geospatial SDK for OpenSceneGraph
 * Copyright 2018 Pelican Mapping
 * http://osgearth.org
 *
 * osgEarth is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 */
#include <osgEarth/Layer>
#include <osgEarth/Cache>
#include <osgEarth/Registry>
#include <osgEarth/SceneGraphCallback>
#include <osgEarth/ShaderLoader>
#include <osgEarth/TileKey>
#include <osgEarth/TerrainResources>
#include <osg/StateSet>

using namespace osgEarth;

#define LC "[Layer] Layer \"" << getName() << "\" "

//.................................................................

Config
Layer::Options::getConfig() const
{
    Config conf = ConfigOptions::getConfig();
    conf.set("name", name());
    conf.set("enabled", enabled());
    conf.set("cacheid", cacheId());
    if (cachePolicy().isSet() && !cachePolicy()->empty())
        conf.set("cache_policy", cachePolicy());
    conf.set("shader_define", shaderDefine());
    conf.set("attribution", attribution());
    conf.set("terrain", terrainPatch());
    conf.set("proxy", _proxySettings );

    for(std::vector<ShaderOptions>::const_iterator i = shaders().begin();
        i != shaders().end();
        ++i)
    {
        conf.add("shader", i->getConfig());
    }

    return conf;
}

void
Layer::Options::fromConfig(const Config& conf)
{
    // defaults:
    _enabled.init(true);
    _terrainPatch.init(false);

    conf.get("name", name());
    conf.get("enabled", enabled());
    conf.get("cache_id", cacheId()); // compat
    conf.get("cacheid", cacheId());
    conf.get("attribution", attribution());
    conf.get("cache_policy", cachePolicy());

    // legacy support:
    if (!cachePolicy().isSet())
    {
        if ( conf.value<bool>( "cache_only", false ) == true )
            _cachePolicy->usage() = CachePolicy::USAGE_CACHE_ONLY;
        if ( conf.value<bool>( "cache_enabled", true ) == false )
            _cachePolicy->usage() = CachePolicy::USAGE_NO_CACHE;
    }
    conf.get("shader_define", shaderDefine());

    const ConfigSet& shadersConf = conf.children("shader");
    for(ConfigSet::const_iterator i = shadersConf.begin(); i != shadersConf.end(); ++i)
        shaders().push_back(ShaderOptions(*i));

    conf.get("terrain", terrainPatch());
    conf.get("patch", terrainPatch());
    conf.get("proxy", _proxySettings );
}

//.................................................................

void
Layer::TraversalCallback::traverse(osg::Node* node, osg::NodeVisitor* nv) const
{
    node->accept(*nv);
}

//.................................................................

Layer::Layer() :
_options(&_optionsConcrete),
_revision(0u)
{
    _uid = osgEarth::Registry::instance()->createUID();
    _renderType = RENDERTYPE_NONE;
    _status = Status::OK();

    init();
}

Layer::Layer(Layer::Options* optionsPtr) :
_options(optionsPtr? optionsPtr : &_optionsConcrete),
_revision(0u)
{
    _uid = osgEarth::Registry::instance()->createUID();
    _renderType = RENDERTYPE_NONE;
    _status = Status::OK();
    // init() will be called by base class
}

Layer::~Layer()
{
    OE_DEBUG << LC << "~Layer\n";
}

void
Layer::bumpRevision()
{
    ++_revision;
}

void
Layer::setReadOptions(const osgDB::Options* readOptions)
{
    // We are storing _cacheSettings both in the Read Options AND
    // as a class member. This is probably not strictly necessary
    // but we will keep the ref in the Layer just to be on the safe
    // side - gw

    _readOptions = Registry::cloneOrCreateOptions(readOptions);

    // Create some local cache settings for this layer:
    CacheSettings* oldSettings = CacheSettings::get(readOptions);
    _cacheSettings = oldSettings ? new CacheSettings(*oldSettings) : new CacheSettings();

    // bring in the new policy for this layer if there is one:
    _cacheSettings->integrateCachePolicy(options().cachePolicy());

    // if caching is a go, install a bin.
    if (_cacheSettings->isCacheEnabled())
    {
        std::string binID = getCacheID();

        // make our cacheing bin!
        CacheBin* bin = _cacheSettings->getCache()->addBin(binID);
        if (bin)
        {
            OE_INFO << LC << "Cache bin is [" << binID << "]\n";
            _cacheSettings->setCacheBin( bin );
        }
        else
        {
            // failed to create the bin, so fall back on no cache mode.
            OE_WARN << LC << "Failed to open a cache bin [" << binID << "], disabling caching\n";
            _cacheSettings->cachePolicy() = CachePolicy::NO_CACHE;
        }
    }

    // Store it for further propagation!
    _cacheSettings->store(_readOptions.get());

    //Store the proxy settings in the options structure.
    if (options().proxySettings().isSet())
    {
        options().proxySettings()->apply(_readOptions.get());
    }
}

const osgDB::Options*
Layer::getReadOptions() const
{
    return _readOptions.get();
}

void
Layer::setCacheID(const std::string& value)
{
    options().cacheId() = value;
}

std::string
Layer::getCacheID() const
{
    std::string binID;
    if (options().cacheId().isSet() && !options().cacheId()->empty())
    {
        binID = options().cacheId().get();
    }
    else
    {
        Config conf = getConfig();
        binID = hashToString(conf.toJSON(false));
    }
    return binID;
}

Config
Layer::getConfig() const
{
    Config conf = options().getConfig();
    conf.key() = getConfigKey();
    return conf;
}

bool
Layer::getEnabled() const
{
    return (options().enabled() == true) && getStatus().isOK();
}

void
Layer::setEnabled(bool value)
{
    if (getStatus().isOK() && value != options().enabled().value())
    {
        options().enabled() = value;
        fireCallback(&LayerCallback::onEnabledChanged);
    }
}

const Status&
Layer::setStatus(const Status& status) const
{
    _status = status;
    return _status;
}

const Status&
Layer::setStatus(const Status::Code& code, const std::string& message) const
{
    return setStatus(Status(code, message));
}

void
Layer::setCachePolicy(const CachePolicy& value)
{
    options().cachePolicy() = value;
}

const CachePolicy&
Layer::getCachePolicy() const
{
    return options().cachePolicy().get();
}

void
Layer::init()
{
    _isOpen = false;

    // For detecting scene graph changes at runtime
    _sceneGraphCallbacks = new SceneGraphCallbacks(this);

    // Copy the layer options name into the Object name.
    // This happens here AND in open.
    if (options().name().isSet())
    {
        osg::Object::setName(options().name().get());
    }

    _internalTime = 0.0;
}

const Status&
Layer::open()
{
    if (_isOpen)
    {
        return getStatus();
    }

    // Copy the layer options name into the Object name.
    if (options().name().isSet())
    {
        osg::Object::setName(options().name().get());
    }

    // Install any shader #defines
    if (options().shaderDefine().isSet() && !options().shaderDefine()->empty())
    {
        OE_INFO << LC << "Setting shader define " << options().shaderDefine().get() << "\n";
        getOrCreateStateSet()->setDefine(options().shaderDefine().get());
    }

    setStatus(openImplementation());

    _isOpen = getStatus().isOK();

    return getStatus();
}

const Status&
Layer::open(const osgDB::Options* readOptions)
{
    setReadOptions(readOptions);
    return open();
}

Status
Layer::openImplementation()
{
    return Status::NoError;
}

Status
Layer::closeImplementation()
{
    return Status::NoError;
}

Status
Layer::close()
{
    Status s = closeImplementation();
    setStatus(Status::OK());
    _isOpen = false;
    return s;
}

bool
Layer::isOpen() const
{
    return _isOpen;
}

const Status&
Layer::getStatus() const
{
    return _status;
}

void
Layer::setTerrainResources(TerrainResources* res)
{
    // Install an earth-file shader if necessary (once)
    for(std::vector<ShaderOptions>::const_iterator i = options().shaders().begin();
        i != options().shaders().end();
        ++i)
    {
        LayerShader* shader = new LayerShader(*i);
        shader->install(this, res);
        _shaders.push_back(shader);
    }
}

void
Layer::setName(const std::string& name)
{
    osg::Object::setName(name);
    options().name() = name;
}

const char*
Layer::getTypeName() const
{
    return typeid(*this).name();
}

#define LAYER_OPTIONS_TAG "osgEarth.LayerOptions"

Layer*
Layer::create(const ConfigOptions& options)
{
    std::string name = options.getConfig().key();

    if (name.empty())
        name = options.getConfig().value("driver");

    if ( name.empty() )
    {
        OE_WARN << "[Layer] ILLEGAL- Layer::create requires a valid driver name" << std::endl;
        return 0L;
    }

    // convey the configuration options:
    osg::ref_ptr<osgDB::Options> dbopt = Registry::instance()->cloneOrCreateOptions();
    dbopt->setPluginData( LAYER_OPTIONS_TAG, (void*)&options );

    //std::string pluginExtension = std::string( ".osgearth_" ) + name;
    std::string pluginExtension = std::string( "." ) + name;

    // use this instead of osgDB::readObjectFile b/c the latter prints a warning msg.
    osgDB::ReaderWriter::ReadResult rr = osgDB::Registry::instance()->readObject( pluginExtension, dbopt.get() );
    if ( !rr.validObject() || rr.error() )
    {
        // quietly fail so we don't get tons of msgs.
        return 0L;
    }

    Layer* layer = dynamic_cast<Layer*>( rr.getObject() );
    if ( layer == 0L )
    {
        // TODO: communicate an error somehow
        return 0L;
    }

    if (layer->getName().empty())
        layer->setName(name);

    rr.takeObject();
    return layer;
}

const ConfigOptions&
Layer::getConfigOptions(const osgDB::Options* options)
{
    static ConfigOptions s_default;
    const void* data = options->getPluginData(LAYER_OPTIONS_TAG);
    return data ? *static_cast<const ConfigOptions*>(data) : s_default;
}

SceneGraphCallbacks*
Layer::getSceneGraphCallbacks() const
{
    return _sceneGraphCallbacks.get();
}

void
Layer::addCallback(LayerCallback* cb)
{
    _callbacks.push_back( cb );
}

void
Layer::removeCallback(LayerCallback* cb)
{
    CallbackVector::iterator i = std::find( _callbacks.begin(), _callbacks.end(), cb );
    if ( i != _callbacks.end() )
        _callbacks.erase( i );
}

void
Layer::apply(osg::Node* node, osg::NodeVisitor* nv) const
{
    if (_traversalCallback.valid())
    {
        _traversalCallback->operator()(node, nv);
    }
    else
    {
        node->accept(*nv);
    }
}

void
Layer::setCullCallback(TraversalCallback* cb)
{
    _traversalCallback = cb;
}

const Layer::TraversalCallback*
Layer::getCullCallback() const
{
    return _traversalCallback.get();
}

const GeoExtent&
Layer::getExtent() const
{
    static GeoExtent s_invalid = GeoExtent::INVALID;
    return s_invalid;
}

osg::StateSet*
Layer::getOrCreateStateSet()
{
    if (!_stateSet.valid())
    {
        _stateSet = new osg::StateSet();
        _stateSet->setName("Layer");
    }
    return _stateSet.get();
}

osg::StateSet*
Layer::getStateSet() const
{
    return _stateSet.get();
}

void
Layer::fireCallback(LayerCallback::MethodPtr method)
{
    for (CallbackVector::iterator i = _callbacks.begin(); i != _callbacks.end(); ++i)
    {
        LayerCallback* cb = dynamic_cast<LayerCallback*>(i->get());
        if (cb) (cb->*method)(this);
    }
}

std::string
Layer::getAttribution() const
{
    // Get the attribution from the layer if it's set.
    if (_options->attribution().isSet())
    {
        return *_options->attribution();
    }
    return "";
}

void
Layer::setAttribution(const std::string& attribution)
{
    _options->attribution() = attribution;
}

void
Layer::resizeGLObjectBuffers(unsigned maxSize)
{
    osg::Object::resizeGLObjectBuffers(maxSize);
    if (getNode())
        getNode()->resizeGLObjectBuffers(maxSize);
    if (getStateSet())
        getStateSet()->resizeGLObjectBuffers(maxSize);
}

void
Layer::releaseGLObjects(osg::State* state) const
{
    osg::Object::releaseGLObjects(state);
    if (getNode())
        getNode()->releaseGLObjects(state);
    if (getStateSet())
        getStateSet()->releaseGLObjects(state);
}

void
Layer::modifyTileBoundingBox(const TileKey& key, osg::BoundingBox& box) const
{
    //NOP
}

const Layer::Hints&
Layer::getHints() const
{
    return _hints;
}

Layer::Hints&
Layer::layerHints()
{
    return _hints;
}

double
Layer::getInternalTime() const
{
    return _internalTime;
}
