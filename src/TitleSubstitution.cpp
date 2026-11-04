#include "TitleSubstitution.h"

#include <string>

#include "FontCharAliases.h"
#include "LuaManager.h"
#include "RageUtil/Regex.h"
#include "XmlFile.h"
#include "XmlFileUtil.h"
#include "global.h"

static const std::string TRANSLATIONS_PATH = "Data/Translations.xml";
static const std::string ERASE_MARKER = "-erase-";

struct TitleTrans {
  Regex title_from_, sub_from_, artist_from_;
  TitleFields replacement_;

  /* If this is true, no translit fields will be generated automatically. */
  bool translit_;
  TitleTrans() { translit_ = true; }

  TitleTrans(const TitleFields& tf, bool translit_)
      : replacement_(tf), translit_(translit_) {}

  bool Matches(const TitleFields& tf, TitleFields& to);

  void LoadFromNode(const XNode* pNode);
};

bool TitleTrans::Matches(const TitleFields& from, TitleFields& to) {
  if (!title_from_.Replace(replacement_.title_, from.title_, to.title_)) {
    return false; /* no match */
  }
  if (!sub_from_.Replace(
          replacement_.subtitle_, from.subtitle_, to.subtitle_)) {
    return false; /* no match */
  }
  if (!artist_from_.Replace(replacement_.artist_, from.artist_, to.artist_)) {
    return false; /* no match */
  }

  return true;
}

void TitleTrans::LoadFromNode(const XNode* pNode) {
  ASSERT(pNode->GetName() == "Translation");

  FOREACH_CONST_Attr(pNode, attr) {
    /* Surround each regex with ^(...)$, to force all comparisons to default
     * to being a full-line match.  (Add ".*" manually if this isn't wanted.) */
    const std::string& sKeyName = attr->first;
    const std::string sValue = attr->second->GetValue<std::string>();
    if (sKeyName == "DontTransliterate") {
      translit_ = false;
    } else if (sKeyName == "TitleFrom") {
      title_from_ = "^(" + sValue + ")$";
    } else if (sKeyName == "ArtistFrom") {
      artist_from_ = "^(" + sValue + ")$";
    } else if (sKeyName == "SubtitleFrom") {
      sub_from_ = "^(" + sValue + ")$";
    } else if (sKeyName == "TitleTo") {
      replacement_.title_ = sValue;
    } else if (sKeyName == "ArtistTo") {
      replacement_.artist_ = sValue;
    } else if (sKeyName == "SubtitleTo") {
      replacement_.subtitle_ = sValue;
    } else if (sKeyName == "TitleTransTo") {
      replacement_.title_translit_ = sValue;
    } else if (sKeyName == "ArtistTransTo") {
      replacement_.artist_translit_ = sValue;
    } else if (sKeyName == "SubtitleTransTo") {
      replacement_.subtitle_translit_ = sValue;
    } else {
      LuaHelpers::ReportScriptErrorFmt(
          "Unknown TitleSubst tag: \"%s\"", sKeyName.c_str());
    }
  }
}

void TitleSubst::AddTrans(const TitleTrans& tr) {
  ASSERT(
      tr.title_from_.IsSet() || tr.sub_from_.IsSet() ||
      tr.artist_from_.IsSet());
  ttab_.push_back(new TitleTrans(tr));
}

void TitleSubst::Subst(TitleFields& tf) {
  for (TitleTrans* tt : ttab_) {
    TitleFields to;
    if (!tt->Matches(tf, to)) {
      continue;
    }

    /* The song matches.  Replace whichever strings aren't empty. */
    if (!tt->replacement_.title_.empty() &&
        tf.title_ != tt->replacement_.title_) {
      if (tt->translit_) {
        tf.title_translit_ = tf.title_;
      }
      tf.title_ =
          (tt->replacement_.title_ != ERASE_MARKER) ? to.title_ : std::string();
      FontCharAliases::ReplaceMarkers(tf.title_);
    }
    if (!tt->replacement_.subtitle_.empty() &&
        tf.subtitle_ != tt->replacement_.subtitle_) {
      if (tt->translit_) {
        tf.subtitle_translit_ = tf.subtitle_;
      }
      tf.subtitle_ = (tt->replacement_.subtitle_ != ERASE_MARKER)
                         ? to.subtitle_
                         : std::string();
      FontCharAliases::ReplaceMarkers(tf.subtitle_);
    }
    if (!tt->replacement_.artist_.empty() &&
        tf.artist_ != tt->replacement_.artist_) {
      if (tt->translit_) {
        tf.artist_translit_ = tf.artist_;
      }
      tf.artist_ = (tt->replacement_.artist_ != ERASE_MARKER) ? to.artist_
                                                              : std::string();
      FontCharAliases::ReplaceMarkers(tf.artist_);
    }

    /* These are used when applying kanji to a field that doesn't have the
     * correct data.  Should be used sparingly. */
    if (!tt->replacement_.title_translit_.empty()) {
      tf.title_translit_ = (tt->replacement_.title_translit_ != ERASE_MARKER)
                               ? tt->replacement_.title_translit_
                               : std::string();
      FontCharAliases::ReplaceMarkers(tf.title_translit_);
    }
    if (!tt->replacement_.subtitle_translit_.empty()) {
      tf.subtitle_translit_ =
          (tt->replacement_.subtitle_translit_ != ERASE_MARKER)
              ? tt->replacement_.subtitle_translit_
              : std::string();
      FontCharAliases::ReplaceMarkers(tf.subtitle_translit_);
    }
    if (!tt->replacement_.artist_translit_.empty()) {
      tf.artist_translit_ = (tt->replacement_.artist_translit_ != ERASE_MARKER)
                                ? tt->replacement_.artist_translit_
                                : std::string();
      FontCharAliases::ReplaceMarkers(tf.artist_translit_);
    }

    // Matched once.  Keep processing to allow multiple matching entries.  For
    // example, allow one entry to translate a title, and another entry to
    // translate the artist.
  }
}

TitleSubst::TitleSubst(const std::string& section) {
  Load(TRANSLATIONS_PATH, section);
}

void TitleSubst::Load(const std::string& filename, const std::string& section) {
  XNode xml;
  if (!XmlFileUtil::LoadFromFileShowErrors(xml, filename)) {
    // LoadFromFile will show its own error
    // LOG->Trace("Error opening %s: %s", filename.c_str(), f.GetError().c_str()
    // );
    return;
  }

  XNode* pGroup = xml.GetChild(section);
  if (pGroup == nullptr) {
    return;
  }
  FOREACH_CONST_Child(pGroup, child) {
    if (child->GetName() != "Translation") {
      continue;
    }

    TitleTrans tr;
    tr.LoadFromNode(child);
    AddTrans(tr);
  }
}

TitleSubst::~TitleSubst() {
  for (unsigned i = 0; i < ttab_.size(); ++i) {
    delete ttab_[i];
  }
}

/*
 * (c) 2003-2004 Glenn Maynard
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
