#ifndef VTZERO_BUILDER_HPP
#define VTZERO_BUILDER_HPP

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <protozero/pbf_builder.hpp>

#include "geometry.hpp"
#include "reader.hpp"
#include "types.hpp"

namespace vtzero {

    using coordinates_type = std::vector<std::pair<int32_t, int32_t>>;

    class layer_builder_base {

    public:

        virtual void build(protozero::pbf_builder<detail::pbf_tile>& pbf_tile_builder) = 0;

    }; // class layer_builder_base

    class layer_builder : public layer_builder_base {

        std::string m_data;
        std::string m_keys_data;
        std::string m_values_data;
        std::map<std::string, uint32_t> m_keys_map;
        std::map<std::string, uint32_t> m_values_map;

        protozero::pbf_builder<detail::pbf_layer> m_pbf_message_layer;
        protozero::pbf_builder<detail::pbf_layer> m_pbf_message_keys;
        protozero::pbf_builder<detail::pbf_layer> m_pbf_message_values;

        uint32_t m_max_key = 0;
        uint32_t m_max_value = 0;

    public:

        template <typename T>
        layer_builder(T&& name, uint32_t version, uint32_t extent) :
            layer_builder_base(),
            m_data(),
            m_keys_data(),
            m_values_data(),
            m_keys_map(),
            m_values_map(),
            m_pbf_message_layer(m_data),
            m_pbf_message_keys(m_keys_data),
            m_pbf_message_values(m_values_data) {
            m_pbf_message_layer.add_uint32(detail::pbf_layer::version, version);
            m_pbf_message_layer.add_string(detail::pbf_layer::name, std::forward<T>(name));
            m_pbf_message_layer.add_uint32(detail::pbf_layer::extent, extent);
        }

        uint32_t add_key(const char* text) {
            auto p = m_keys_map.insert(std::make_pair(text, m_max_key));

            if (p.second) {
                ++m_max_key;
                m_pbf_message_keys.add_string(detail::pbf_layer::keys, text);
            }

            return p.first->second;
        }

        uint32_t add_key(const data_view& text) {
            auto p = m_keys_map.insert(std::make_pair(std::string{text.data(), text.size()}, m_max_key));

            if (p.second) {
                ++m_max_key;
                m_pbf_message_keys.add_string(detail::pbf_layer::keys, text);
            }

            return p.first->second;
        }

        uint32_t add_value(const char* text) {
            auto p = m_values_map.insert(std::make_pair(text, m_max_value));

            if (p.second) {
                ++m_max_value;

                protozero::pbf_builder<detail::pbf_value> pbf_message_value{m_pbf_message_values, detail::pbf_layer::values};
                pbf_message_value.add_string(detail::pbf_value::string_value, text);
            }

            return p.first->second;
        }

        uint32_t add_value(const data_view& text) {
            auto p = m_values_map.insert(std::make_pair(std::string{text.data(), text.size()}, m_max_value));

            if (p.second) {
                ++m_max_value;

                m_pbf_message_values.add_string(detail::pbf_layer::values, text);
            }

            return p.first->second;
        }

        const std::string& data() const noexcept {
            return m_data;
        }

        const std::string& keys_data() const noexcept {
            return m_keys_data;
        }

        const std::string& values_data() const noexcept {
            return m_values_data;
        }

        protozero::pbf_builder<detail::pbf_layer>& message() noexcept {
            return m_pbf_message_layer;
        }

        void add_feature(feature& feature, layer& layer);

        void build(protozero::pbf_builder<detail::pbf_tile>& pbf_tile_builder) override {
            pbf_tile_builder.add_bytes_vectored(detail::pbf_tile::layers,
                data(),
                keys_data(),
                values_data()
            );
        }

    }; // class layer_builder

    class layer_builder_existing : public layer_builder_base {

        data_view m_data;

    public:

        layer_builder_existing(const data_view& data) :
            layer_builder_base(),
            m_data(data) {
        }

        void build(protozero::pbf_builder<detail::pbf_tile>& pbf_tile_builder) override {
            pbf_tile_builder.add_bytes(detail::pbf_tile::layers, m_data.data(), m_data.size());
        }

    }; // class layer_builder_existing

    class feature_builder {

        layer_builder& m_layer;

    protected:

        protozero::pbf_builder<detail::pbf_feature> m_feature_writer;
        std::unique_ptr<protozero::packed_field_uint32> m_pbf_tags{nullptr};

        feature_builder(layer_builder& layer, uint64_t id) :
            m_layer(layer),
            m_feature_writer(layer.message(), detail::pbf_layer::features) {
            m_feature_writer.add_uint64(detail::pbf_feature::id, id);
        }

        template <typename TKey, typename TValue>
        void add_tags(TKey&& key, TValue&& value) {
            const auto k = m_layer.add_key(std::forward<TKey>(key));
            const auto v = m_layer.add_value(std::forward<TValue>(value));
            m_pbf_tags->add_element(k);
            m_pbf_tags->add_element(v);
        }

    }; // class feature_builder

    class geometry_feature_builder : public feature_builder {

    public:

        geometry_feature_builder(layer_builder& layer, uint64_t id, GeomType geom_type, const data_view& geometry) :
            feature_builder(layer, id) {
            m_feature_writer.add_enum(detail::pbf_feature::type, static_cast<int32_t>(geom_type));
            m_feature_writer.add_string(detail::pbf_feature::geometry, geometry);
            m_pbf_tags.reset(new protozero::packed_field_uint32{m_feature_writer, detail::pbf_feature::tags});
        }

        template <typename TKey, typename TValue>
        void add_attribute(TKey&& key, TValue&& value) {
            add_tags(std::forward<TKey>(key), std::forward<TValue>(value));
        }

    }; // class geometry_feature_builder

    class point_feature_builder : public feature_builder {

        void add_point(point p) {
            protozero::packed_field_uint32 geometry_field{m_feature_writer, detail::pbf_feature::geometry};
            geometry_field.add_element(detail::command_move_to(1));
            geometry_field.add_element(protozero::encode_zigzag32(p.x));
            geometry_field.add_element(protozero::encode_zigzag32(p.y));
        }

    public:

        point_feature_builder(layer_builder& layer, uint64_t id, point p) :
            feature_builder(layer, id) {
            m_feature_writer.add_enum(detail::pbf_feature::type, static_cast<int32_t>(GeomType::POINT));
            add_point(p);
            m_pbf_tags.reset(new protozero::packed_field_uint32{m_feature_writer, detail::pbf_feature::tags});
        }

        template <typename TKey, typename TValue>
        void add_attribute(TKey&& key, TValue&& value) {
            add_tags(std::forward<TKey>(key), std::forward<TValue>(value));
        }

    }; // class point_feature_builder

    class line_string_feature_builder : public feature_builder {

        std::unique_ptr<protozero::packed_field_uint32> m_pbf_geometry{nullptr};
        size_t m_num_points = 0;
        point m_cursor{0, 0};
        bool m_start_line = false;

    public:

        line_string_feature_builder(layer_builder& layer, uint64_t id) :
            feature_builder(layer, id) {
            m_feature_writer.add_enum(detail::pbf_feature::type, static_cast<int32_t>(GeomType::LINESTRING));
            m_pbf_tags.reset(new protozero::packed_field_uint32{m_feature_writer, detail::pbf_feature::tags});
        }

        ~line_string_feature_builder() {
            assert(m_num_points == 0 && "LineString has fewer points than expected");
        }

        template <typename TKey, typename TValue>
        void add_attribute(TKey&& key, TValue&& value) {
            assert(m_pbf_tags && "Call add_attribute() for all attributes first, then add geometry data");
            add_tags(std::forward<TKey>(key), std::forward<TValue>(value));
        }

        void start_linestring(size_t num_points) {
            assert(num_points > 1);
            assert(m_num_points == 0 && "LineString has fewer points than expected");
            m_num_points = num_points;
            if (m_pbf_tags) {
                m_pbf_tags.reset();
            }
            if (!m_pbf_geometry) {
                m_pbf_geometry.reset(new protozero::packed_field_uint32{m_feature_writer, detail::pbf_feature::geometry});
            }
            m_start_line = true;
        }

        void add_point(const point& p) {
            assert(!m_pbf_tags && "Call start_linestring() before add_point()");
            assert(m_num_points > 0 && "Too many calls to add_point()");
            --m_num_points;
            if (m_start_line) {
                m_pbf_geometry->add_element(detail::command_move_to(1));
                m_pbf_geometry->add_element(protozero::encode_zigzag32(p.x - m_cursor.x));
                m_pbf_geometry->add_element(protozero::encode_zigzag32(p.y - m_cursor.y));
                m_pbf_geometry->add_element(detail::command_line_to(m_num_points));
                m_start_line = false;
            } else {
                assert(p != m_cursor);
                m_pbf_geometry->add_element(protozero::encode_zigzag32(p.x - m_cursor.x));
                m_pbf_geometry->add_element(protozero::encode_zigzag32(p.y - m_cursor.y));
            }
            m_cursor = p;
        }

    }; // class line_string_feature_builder

    inline void layer_builder::add_feature(feature& feature, layer& layer) {
        geometry_feature_builder feature_builder{*this, feature.id(), feature.type(), feature.geometry()};
        for (auto tag : feature.tags(layer)) {
            feature_builder.add_attribute(tag.key(), tag.value());
        }
    }

    class tile_builder {

        std::vector<std::unique_ptr<layer_builder_base>> m_layers;

    public:

        layer_builder& add_layer(const layer& layer) {
            m_layers.emplace_back(new layer_builder{layer.name(), layer.version(), layer.extent()});
            return *static_cast<layer_builder*>(m_layers.back().get());
        }

        template <typename T,
                  typename std::enable_if<!std::is_same<typename std::decay<T>::type, layer>{}, int>::type = 0>
        layer_builder& add_layer(T&& name, uint32_t version = 2, uint32_t extent = 4096) {
            m_layers.emplace_back(new layer_builder{std::forward<T>(name), version, extent});
            return *static_cast<layer_builder*>(m_layers.back().get());
        }

        void add_layer_with_data(const layer& layer) {
            m_layers.emplace_back(new layer_builder_existing{layer.data()});
        }

        std::string serialize() {
            std::string data;

            protozero::pbf_builder<detail::pbf_tile> pbf_tile_builder{data};
            for (auto& layer : m_layers) {
                layer->build(pbf_tile_builder);
            }

            return data;
        }

    }; // class tile_builder

} // namespace vtzero

#endif // VTZERO_BUILDER_HPP
