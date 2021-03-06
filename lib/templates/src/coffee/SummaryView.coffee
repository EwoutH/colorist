# ---------------------------------------------------------------------------
#                         Copyright Joe Drago 2018.
#         Distributed under the Boost Software License, Version 1.0.
#            (See accompanying file LICENSE_1_0.txt or copy at
#                  http://www.boost.org/LICENSE_1_0.txt)
# ---------------------------------------------------------------------------

React = require 'react'
DOM = require 'react-dom'

RaisedButton = require('material-ui/RaisedButton').default

tags = require './tags'
utils = require './utils'
{el, div, table, tr, td, tbody} = require './tags'

TopBar = require './TopBar'

section = (key, elements) ->
  return table {
    key: "section_table_#{key}"
    style:
      fontFamily: 'monospace'
  }, [
    tbody {
      key: "section_tbody_#{key}"
    }, elements
  ]

heading = (title) ->
  return tr {
    key: "heading_tr_#{title}"
  }, [
    td {
      key: "heading_td_#{title}"
      colSpan: 3
      style:
        fontWeight: 900
        fontSize: '1.4em'
        borderBottom: '1px solid black'
    }, title
  ]

pair = (indent, key, value) ->
  colon = if value then ":" else ""
  return tr {
      key: "pair_tr_#{key}"
  }, [
    td {
      key: "pair_key_#{key}"
      style:
        fontWeight: 900
        paddingLeft: indent * 20
    }, key
    td {
      key: "pair_colon_#{key}"
      style:
        fontWeight: 900
    }, colon
    td {
      key: "pair_value_#{key}"
    }, value
  ]

class SummaryView extends React.Component
  @defaultProps:
    app: null

  constructor: (props) ->
    super props

  render: ->
    elements = []

    D = COLORIST_DATA

    if D.icc.luminance > 0
      lumText = "#{D.icc.luminance} nits - (lumi tag present)"
    else
      if D.icc.curveType == 'hlg'
        lumText = "Unspecified - (HLG using max #{D.srgb.hlgLuminance} nits, from diffuse white of #{D.srgb.highlightLuminance} nits)"
      else
        lumText = "Unspecified - (using default: #{D.srgb.highlightLuminance} nits)"

    elements.push section "basic", [
      heading "Basic info"
      pair 0, "Filename", D.filename
      pair 0, "Dimensions", "#{D.width}x#{D.height}"
      pair 0, "Bit Depth", "#{D.depth}-bits per channel"
      pair 0, "ICC Profile", ""
      pair 1, "Description", D.icc.description
      pair 1, "Primaries", ""
      pair 2, "Red",   "#{utils.fr(D.icc.primaries[0], 4)}, #{utils.fr(D.icc.primaries[1], 4)}"
      pair 2, "Green", "#{utils.fr(D.icc.primaries[2], 4)}, #{utils.fr(D.icc.primaries[3], 4)}"
      pair 2, "Blue",  "#{utils.fr(D.icc.primaries[4], 4)}, #{utils.fr(D.icc.primaries[5], 4)}"
      pair 1, "White Point", "#{utils.fr(D.icc.primaries[6], 4)}, #{utils.fr(D.icc.primaries[7], 4)}"
      pair 1, "Curve Type", D.icc.curveType
      pair 1, "Max Luminance", lumText
      pair 0, "sRGB Overranging (#{D.srgb.highlightLuminance} nits)", ""
      pair 1, "Total Pixels", "#{D.srgb.pixelCount}"
      pair 1, "Total HDR Pixels", "#{D.srgb.hdrPixelCount} (#{utils.fr(100 * D.srgb.hdrPixelCount / D.srgb.pixelCount, 2)}%)"
      pair 2, "Overbright Pixels", "#{D.srgb.overbrightPixelCount} (#{utils.fr(100 * D.srgb.overbrightPixelCount / D.srgb.pixelCount, 2)}%)"
      pair 2, "Out of Gamut Pixels", "#{D.srgb.outOfGamutPixelCount} (#{utils.fr(100 * D.srgb.outOfGamutPixelCount / D.srgb.pixelCount, 2)}%)"
      pair 2, "Both OB and OOG", "#{D.srgb.bothPixelCount} (#{utils.fr(100 * D.srgb.bothPixelCount / D.srgb.pixelCount, 2)}%)"
      pair 1, "Brightest Pixel", "#{utils.fr(D.srgb.brightestPixelNits, 1)} nits"
      pair 2, "Coord", "(#{D.srgb.brightestPixelX}, #{D.srgb.brightestPixelY})"
    ]

    elements.push el RaisedButton, {
      key: "button.pixels"
      style:
        margin: 12
      label: "View pixels"
      primary: true
      onClick: =>
        @props.app.redirect('#pixels')
    }

    elements.push el RaisedButton, {
      key: "button.srgb"
      style:
        margin: 12
      label: "View SRGB Highlight (over #{D.srgb.highlightLuminance} nits)"
      primary: true
      onClick: =>
        @props.app.redirect('#srgb')
    }

    outerElements = []
    outerElements.push el TopBar, {
      key: "TopBar"
      title: "Summary"
      app: @props.app
    }
    outerElements.push div {
      key: "outermargin"
      style:
        margin: '20px'
    }, elements
    return outerElements

module.exports = SummaryView
