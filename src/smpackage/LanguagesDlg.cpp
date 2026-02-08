// LanguagesDlg.cpp : implementation file

#define CO_EXIST_WITH_MFC
#include "global.h"
#include "stdafx.h"
#include "smpackage.h"
#include "LanguagesDlg.h"
#include "SpecialFiles.h"
#include "RageUtil.h"
#include "RageUtil/Regex.h"
#include "IniFile.h"
#include "languagesdlg.h"
#include "SMPackageUtil.h"
#include "CreateLanguageDlg.h"
#include "RageFile.h"
#include "languagesdlg.h"
#include "RageFileManager.h"
#include ".\languagesdlg.h"
#include "CsvFile.h"
#include "archutils/Win32/DialogUtil.h"
#include "LocalizedString.h"
#include "arch/Dialog/Dialog.h"
#include "archutils/Win32/SpecialDirs.h"
#include "archutils/Win32/ErrorStrings.h"

#include <vector>


// LanguagesDlg dialog

IMPLEMENT_DYNAMIC(LanguagesDlg, CDialog)
LanguagesDlg::LanguagesDlg(CWnd* pParent /*=NULL*/)
	: CDialog(LanguagesDlg::IDD, pParent)
{
}

LanguagesDlg::~LanguagesDlg()
{
}

void LanguagesDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LIST_THEMES, m_listThemes);
	DDX_Control(pDX, IDC_LIST_LANGUAGES, m_listLanguages);
	DDX_Control(pDX, IDC_CHECK_EXPORT_ALREADY_TRANSLATED, m_buttonExportAlreadyTranslated);
}

BOOL LanguagesDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	DialogUtil::LocalizeDialogAndContents( *this );

	std::vector<std::string> vs;
	GetDirListing( SpecialFiles::THEMES_DIR+"*", vs, true );
	StripCvsAndSvn( vs );
	for (std::vector<std::string>::const_iterator s = vs.begin(); s != vs.end(); ++s)
		m_listThemes.AddString( *s );
	if( !vs.empty() )
		m_listThemes.SetSel( 0 );

	OnSelchangeListThemes();

	return TRUE;  // return TRUE  unless you set the focus to a control
}

static std::string GetCurrentString( const CListBox &list )
{
	// TODO: Add your control notification handler code here
	int iSel = list.GetCurSel();
	if( iSel == LB_ERR )
		return std::string();
	CString s;
	list.GetText( list.GetCurSel(), s );
	return std::string( s );
}

static void SelectString( CListBox &list, const std::string &sToSelect )
{
	for( int i=0; i<list.GetCount(); i++ )
	{
		CString s;
		list.GetText( i, s );
		if( s == sToSelect )
		{
			list.SetCurSel( i );
			break;
		}
	}
}

void LanguagesDlg::OnSelchangeListThemes()
{
	// TODO: Add your control notification handler code here
	m_listLanguages.ResetContent();

	std::string sTheme = GetCurrentString( m_listThemes );
	if( !sTheme.empty() )
	{
		std::string sLanguagesDir = SpecialFiles::THEMES_DIR + sTheme + "/" + SpecialFiles::LANGUAGES_SUBDIR;

		std::vector<std::string> vs;
		GetDirListing( sLanguagesDir+"*.ini", vs, false );
		for (std::vector<std::string>::const_iterator s = vs.begin(); s != vs.end(); ++s)
		{
			std::string sIsoCode = GetFileNameWithoutExtension(*s);
			std::string sLanguage = SMPackageUtil::GetLanguageDisplayString(sIsoCode);
			m_listLanguages.AddString( ConvertUTF8ToACP(sLanguage) );
		}
		if( !vs.empty() )
			m_listLanguages.SetSel( 0 );
	}

	OnSelchangeListLanguages();
}

static std::string GetLanguageFile( const std::string &sTheme, const std::string &sLanguage )
{
	return SpecialFiles::THEMES_DIR + sTheme + "/" + SpecialFiles::LANGUAGES_SUBDIR + sLanguage + ".ini";
}

static int GetNumValuesInIniFile( const std::string &sIniFile )
{
	int count = 0;
	IniFile ini;
	ini.ReadFile( sIniFile );
	FOREACH_CONST_Child( &ini, key )
	{
		FOREACH_CONST_Attr( key, value )
			count++;
	}
	return count;
}

static int GetNumIntersectingIniValues( const std::string &sIniFile1, const std::string &sIniFile2 )
{
	int count = 0;
	IniFile ini1;
	ini1.ReadFile( sIniFile1 );
	IniFile ini2;
	ini2.ReadFile( sIniFile2 );
	FOREACH_CONST_Child( &ini1, key1 )
	{
		const XNode *key2 = ini2.GetChild( key1->m_sName );
		if( key2 == NULL )
			continue;
		FOREACH_CONST_Attr( key1, attr1 )
		{
			if( key2->GetAttr(attr1->first) == NULL)
				continue;
			count++;
		}
	}
	return count;
}

void LanguagesDlg::OnSelchangeListLanguages()
{
	// TODO: Add your control notification handler code here
	int iTotalStrings = -1;
	int iNeedTranslation = -1;

	std::string sTheme = GetCurrentString( m_listThemes );
	std::string sLanguage = GetCurrentString( m_listLanguages );

	if( !sTheme.empty() )
	{
		std::string sBaseLanguageFile = GetLanguageFile( sTheme, SpecialFiles::BASE_LANGUAGE );
		iTotalStrings = GetNumValuesInIniFile( sBaseLanguageFile );

		if( !sLanguage.empty() )
		{
			sLanguage = SMPackageUtil::GetLanguageCodeFromDisplayString( sLanguage );

			std::string sLanguageFile = GetLanguageFile( sTheme, sLanguage );
			iNeedTranslation = iTotalStrings - GetNumIntersectingIniValues( sBaseLanguageFile, sLanguageFile );
		}
	}

	GetDlgItem(IDC_STATIC_TOTAL_STRINGS		)->SetWindowText( ssprintf(iTotalStrings==-1?"":"%d",iTotalStrings) );
	GetDlgItem(IDC_STATIC_NEED_TRANSLATION	)->SetWindowText( ssprintf(iNeedTranslation==-1?"":"%d",iNeedTranslation) );

	GetDlgItem(IDC_BUTTON_CREATE)->EnableWindow( !sTheme.empty() );
	GetDlgItem(IDC_BUTTON_DELETE)->EnableWindow( !sLanguage.empty() );
	GetDlgItem(IDC_BUTTON_EXPORT)->EnableWindow( !sLanguage.empty() );
	GetDlgItem(IDC_BUTTON_IMPORT)->EnableWindow( !sLanguage.empty() );
	GetDlgItem(IDC_CHECK_LANGUAGE)->EnableWindow( !sLanguage.empty() );
	GetDlgItem(IDC_CHECK_EXPORT_ALREADY_TRANSLATED)->EnableWindow( !sLanguage.empty() );
}


BEGIN_MESSAGE_MAP(LanguagesDlg, CDialog)
	ON_LBN_SELCHANGE(IDC_LIST_THEMES, OnSelchangeListThemes)
	ON_LBN_SELCHANGE(IDC_LIST_LANGUAGES, OnSelchangeListLanguages)
	ON_BN_CLICKED(IDC_BUTTON_CREATE, OnBnClickedButtonCreate)
	ON_BN_CLICKED(IDC_BUTTON_DELETE, OnBnClickedButtonDelete)
	ON_BN_CLICKED(IDC_BUTTON_EXPORT, OnBnClickedButtonExport)
	ON_BN_CLICKED(IDC_BUTTON_IMPORT, OnBnClickedButtonImport)
	ON_BN_CLICKED(IDC_CHECK_LANGUAGE, OnBnClickedCheckLanguage)
END_MESSAGE_MAP()


// LanguagesDlg message handlers

void LanguagesDlg::OnBnClickedButtonCreate()
{
	// TODO: Add your control notification handler code here
	CreateLanguageDlg dlg;
	int nResponse = dlg.DoModal();
	if( nResponse != IDOK )
		return;

	std::string sTheme = GetCurrentString( m_listThemes );
	ASSERT( !sTheme.empty() );

	std::string sLanguageFile = GetLanguageFile( sTheme, std::string(dlg.m_sChosenLanguageCode) );

	// create empty file
	RageFile file;
	file.Open( sLanguageFile, RageFile::WRITE );
	file.Close();	// flush file

	OnSelchangeListThemes();
	SelectString( m_listLanguages, SMPackageUtil::GetLanguageDisplayString(std::string(dlg.m_sChosenLanguageCode)) );
	OnSelchangeListLanguages();
}

static LocalizedString THIS_WILL_PERMANENTLY_DELETE( "LanguagesDlg", "This will permanently delete '%s'. Continue?" );
void LanguagesDlg::OnBnClickedButtonDelete()
{
	// TODO: Add your control notification handler code here
	std::string sTheme = GetCurrentString( m_listThemes );
	ASSERT( !sTheme.empty() );
	std::string sLanguage = GetCurrentString( m_listLanguages );
	ASSERT( !sLanguage.empty() );
	sLanguage = SMPackageUtil::GetLanguageCodeFromDisplayString( sLanguage );

	std::string sLanguageFile = GetLanguageFile( sTheme, sLanguage );

	std::string sMessage = ssprintf(THIS_WILL_PERMANENTLY_DELETE.GetValue(),sLanguageFile.c_str());
	Dialog::Result ret = Dialog::AbortRetry( sMessage );
	if( ret != Dialog::retry )
		return;

	FILEMAN->Remove( sLanguageFile );

	OnSelchangeListThemes();
}

struct TranslationLine
{
	std::string sSection, sID, sBaseLanguage, sCurrentLanguage;
};

static LocalizedString FAILED_TO_SAVE			( "LanguagesDlg", "Failed to save '%s'." );
static LocalizedString THERE_ARE_NO_STRINGS_TO_EXPORT	( "LanguagesDlg", "There are no strings to export for this language." );
static LocalizedString EXPORTED_TO_YOUR_DESKTOP		( "LanguagesDlg", "Exported to your Desktop as '%s'." );
void LanguagesDlg::OnBnClickedButtonExport()
{
	// TODO: Add your control notification handler code here
	std::string sTheme = GetCurrentString( m_listThemes );
	ASSERT( !sTheme.empty() );
	std::string sLanguage = GetCurrentString( m_listLanguages );
	ASSERT( !sLanguage.empty() );
	sLanguage = SMPackageUtil::GetLanguageCodeFromDisplayString( sLanguage );

	std::string sBaseLanguageFile = GetLanguageFile( sTheme, SpecialFiles::BASE_LANGUAGE );
	std::string sLanguageFile = GetLanguageFile( sTheme, sLanguage );

	bool bExportAlreadyTranslated = !!m_buttonExportAlreadyTranslated.GetCheck();

	CsvFile csv;
	IniFile ini1;
	ini1.ReadFile( sBaseLanguageFile );
	IniFile ini2;
	ini2.ReadFile( sLanguageFile );
	int iNumExpored = 0;

	std::vector<std::string> vs;
	vs.push_back( "Section" );
	vs.push_back( "ID" );
	vs.push_back( "Base Language Value" );
	vs.push_back( "Localized Value" );
	csv.m_vvs.push_back( vs );

	FOREACH_CONST_Child( &ini1, key )
	{
		FOREACH_CONST_Attr( key, value )
		{
			TranslationLine tl;
			tl.sSection = key->m_sName;
			tl.sID = value->first;
			tl.sBaseLanguage = value->second->GetValue<std::string>();
			ini2.GetValue( tl.sSection, tl.sID, tl.sCurrentLanguage );

			// don't export empty strings
			std::string sBaseLanguageTrimmed = tl.sBaseLanguage;
			TrimLeft( sBaseLanguageTrimmed, " " );
			TrimRight( sBaseLanguageTrimmed, " " );
			if( sBaseLanguageTrimmed.empty() )
				continue;

			bool bAlreadyTranslated = !tl.sCurrentLanguage.empty();
			if( !bAlreadyTranslated || bExportAlreadyTranslated )
			{
				std::vector<std::string> vs;
				vs.push_back( tl.sSection );
				vs.push_back( tl.sID );
				vs.push_back( tl.sBaseLanguage );
				vs.push_back( tl.sCurrentLanguage );
				csv.m_vvs.push_back( vs );
				iNumExpored++;
			}
		}
	}

	RageFileOsAbsolute file;
	std::string sFile = sTheme+"-"+sLanguage+".csv";
	std::string sFullFile = SpecialDirs::GetDesktopDir() + sFile;
	file.Open( sFullFile, RageFile::WRITE );
	if( iNumExpored == 0 )
	{
		Dialog::OK( THERE_ARE_NO_STRINGS_TO_EXPORT.GetValue() );
		return;
	}

	if( csv.WriteFile(file) )
		Dialog::OK( ssprintf(EXPORTED_TO_YOUR_DESKTOP.GetValue(),sFile.c_str()) );
	else
		Dialog::OK( ssprintf(FAILED_TO_SAVE.GetValue(),sFullFile.c_str()) );
}

static LocalizedString ERROR_READING_FILE			( "LanguagesDlg", "Error reading file '%s'." );
static LocalizedString ERROR_PARSING_FILE			( "LanguagesDlg", "Error parsing file '%s'." );
static LocalizedString IMPORTING_THESE_STRINGS_WILL_OVERRIDE	( "LanguagesDlg", "Importing these strings will override all data in '%s'. Continue?" );
static LocalizedString ERROR_READING				( "LanguagesDlg", "Error reading '%s'." );
static LocalizedString ERROR_READING_EACH_LINE_MUST_HAVE	( "LanguagesDlg", "Error reading line '%s' from '%s': Each line must have either 3 or 4 values.  This row has %d values." );
static LocalizedString IMPORTED_STRINGS_INTO			( "LanguagesDlg", "Imported %d strings into '%s'.\n - %d were added.\n - %d were modified\n - %d were unchanged\n - %d empty strings were ignored" );
void LanguagesDlg::OnBnClickedButtonImport()
{
	// TODO: Add your control notification handler code here
	CFileDialog dialog (
		TRUE,	// file open?
		NULL,	// default file extension
		NULL,	// default file name
		OFN_HIDEREADONLY | OFN_NOCHANGEDIR,		// flags
		"CSV file (*.csv)|*.csv|||"
		);
	int iRet = dialog.DoModal();
	std::string sCsvFile = dialog.GetPathName();
	if( iRet != IDOK )
		return;

	std::string sTheme = GetCurrentString( m_listThemes );
	ASSERT( !sTheme.empty() );
	std::string sLanguage = GetCurrentString( m_listLanguages );
	ASSERT( !sLanguage.empty() );
	sLanguage = SMPackageUtil::GetLanguageCodeFromDisplayString( sLanguage );

	RageFileOsAbsolute cvsFile;
	if( !cvsFile.Open(sCsvFile) )
	{
		Dialog::OK( ssprintf(ERROR_READING_FILE.GetValue(),sCsvFile.c_str()) );
		return;
	}
	CsvFile csv;
	if( !csv.ReadFile(cvsFile) )
	{
		Dialog::OK( ssprintf(ERROR_PARSING_FILE.GetValue(),sCsvFile.c_str()) );
		return;
	}

	std::string sLanguageFile = GetLanguageFile( sTheme, sLanguage );

	{
		Dialog::Result ret = Dialog::AbortRetry( ssprintf(IMPORTING_THESE_STRINGS_WILL_OVERRIDE.GetValue(),sLanguageFile.c_str()) );
		if( ret != Dialog::retry )
			return;
	}

	IniFile ini;
	if( !ini.ReadFile(sLanguageFile) )
	{
		Dialog::OK( ssprintf(ERROR_READING.GetValue(),sLanguageFile.c_str()) );
		return;
	}

	int iNumImported = 0;
	int iNumAdded = 0;
	int iNumModified = 0;
	int iNumUnchanged = 0;
	int iNumIgnored = 0;
	for (std::vector<CsvFile::StringVector>::const_iterator line = csv.m_vvs.begin(); line != csv.m_vvs.end(); ++line)
	{
		int iLineIndex = line - csv.m_vvs.begin();

		// Skip the header row
		if( iLineIndex == 0 )
			continue;

		TranslationLine tl;
		int iNumValues = line->size();
		if( iNumValues != 3 && iNumValues != 4 )
		{
			Dialog::OK( ssprintf(ERROR_READING_EACH_LINE_MUST_HAVE.GetValue(),
				join(",",*line).c_str(),
				sCsvFile.c_str(),
				iNumValues) );
			return;
		}
		tl.sSection = (*line)[0];
		tl.sID = (*line)[1];
		tl.sBaseLanguage = (*line)[2];
		tl.sCurrentLanguage = iNumValues == 4 ? (*line)[3] : std::string();

		if( tl.sCurrentLanguage.empty() )
		{
			iNumIgnored++;
		}
		else
		{
			std::string sOldCurrentLanguage;
			bool bExists = ini.GetValue( tl.sSection, tl.sID, sOldCurrentLanguage );
			if( bExists )
			{
				if( sOldCurrentLanguage == tl.sCurrentLanguage )
					iNumUnchanged++;
				else
					iNumModified++;
			}
			else
			{
				iNumAdded++;
			}

			ini.SetValue( tl.sSection, tl.sID, tl.sCurrentLanguage );
			iNumImported++;
		}
	}

	if( ini.WriteFile(sLanguageFile) )
		Dialog::OK( ssprintf(IMPORTED_STRINGS_INTO.GetValue(),iNumImported,sLanguageFile.c_str(),iNumAdded,iNumModified,iNumUnchanged,iNumIgnored) );
	else
		Dialog::OK( ssprintf(FAILED_TO_SAVE.GetValue(),sLanguageFile.c_str()) );

	OnSelchangeListThemes();
}

void GetAllMatches( const std::string &sRegex, const std::string &sString, std::vector<std::string> &asOut )
{
	Regex re( sRegex + "(.*)$");
	std::string sMatch( sString );
	while(1)
	{
		std::vector<std::string> asMatches;
		if( !re.Compare(sMatch, asMatches) )
			break;
		asOut.push_back( asMatches[0] );
		sMatch = asMatches[1];
	}
}

void DumpList( const std::vector<std::string> &asList, RageFile &file )
{
	std::string sLine;
	for (std::vector<std::string>::const_iterator s = asList.begin(); s != asList.end(); ++s)
	{
		if( sLine.size() + s->size() > 100 )
		{
			file.PutLine( sLine );
			sLine = "";
		}

		if( sLine.size() )
			sLine += ", ";
		else
			sLine += "    ";

		sLine += *s;
	}

	file.PutLine( sLine );
}

void LanguagesDlg::OnBnClickedCheckLanguage()
{
	std::string sTheme = GetCurrentString( m_listThemes );
	ASSERT( !sTheme.empty() );
	std::string sLanguage = GetCurrentString( m_listLanguages );
	ASSERT( !sLanguage.empty() );
	sLanguage = SMPackageUtil::GetLanguageCodeFromDisplayString( sLanguage );

	std::string sBaseLanguageFile = GetLanguageFile( sTheme, SpecialFiles::BASE_LANGUAGE );
	std::string sLanguageFile = GetLanguageFile( sTheme, sLanguage );

	IniFile ini1;
	ini1.ReadFile( sBaseLanguageFile );
	IniFile ini2;
	ini2.ReadFile( sLanguageFile );

	RageFileOsAbsolute file;
	std::string sFile = sTheme+"-"+sLanguage+" check.txt";
	std::string sFullFile = SpecialDirs::GetDesktopDir() + sFile;
	file.Open( sFullFile, RageFile::WRITE );

	{
		FOREACH_CONST_Child( &ini1, key )
		{
			FOREACH_CONST_Attr( key, value )
			{
				const std::string &sSection = key->m_sName;
				const std::string &sID = value->first;
				const std::string &sBaseLanguage = value->second->GetValue<std::string>();
				std::string sCurrentLanguage;
				ini2.GetValue( sSection, sID, sCurrentLanguage );

				if( sCurrentLanguage.empty() )
					continue;

				/* Check &FOO;, %{foo}, %-1.2f, %.  Some mismatches here are normal, particularly in
				 * languages with substantially different word order rules than English. */
				{
					std::string sRegex = "(&[A-Za-z]+;|%{[A-Za-z]+}|%[+-]?[0-9]*.?[0-9]*[sidf]|%)";
					std::vector<std::string> asMatchesBase;
					GetAllMatches( sRegex, sBaseLanguage, asMatchesBase );

					std::vector<std::string> asMatches;
					GetAllMatches( sRegex, sCurrentLanguage, asMatches );

					if( asMatchesBase.size() != asMatches.size() ||
						!std::equal(asMatchesBase.begin(), asMatchesBase.end(), asMatches.begin()) )
					{
						file.PutLine( ssprintf("Entity/substitution mismatch in section [%s] (%s):", sSection.c_str(), sID.c_str()) );
						file.PutLine( ssprintf("    %s", sCurrentLanguage.c_str()) );
						file.PutLine( ssprintf("    %s", sBaseLanguage.c_str()) );
					}
				}

				/* Check "foo::bar", "foo\nbar" and double quotes.  Check these independently
				 * of the above. */
				{
					std::string sRegex = "(\"|::|\\n)";
					std::vector<std::string> asMatchesBase;
					GetAllMatches( sRegex, sBaseLanguage, asMatchesBase );

					std::vector<std::string> asMatches;
					GetAllMatches( sRegex, sCurrentLanguage, asMatches );

					if( asMatchesBase.size() != asMatches.size() ||
						!std::equal(asMatchesBase.begin(), asMatchesBase.end(), asMatches.begin()) )
					{
						file.PutLine( ssprintf("Quote/line break mismatch in section [%s] (%s):", sSection.c_str(), sID.c_str()) );
						file.PutLine( ssprintf("    %s", sCurrentLanguage.c_str()) );
						file.PutLine( ssprintf("    %s", sBaseLanguage.c_str()) );
					}
				}

				/* Check that both end in a period or both don't end an a period. */
				{
					std::string sBaseLanguage2 = sBaseLanguage;
					TrimRight( sBaseLanguage2, " " );
					std::string sCurrentLanguage2 = sCurrentLanguage;
					TrimRight( sCurrentLanguage2, " " );

					if( (sBaseLanguage2.Right(1) == ".")  ^  (sCurrentLanguage2.Right(1) == ".") )
					{
						file.PutLine( ssprintf("Period mismatch in section [%s] (%s):", sSection.c_str(), sID.c_str()) );
						file.PutLine( ssprintf("    %s", sCurrentLanguage.c_str()) );
						file.PutLine( ssprintf("    %s", sBaseLanguage.c_str()) );
					}
				}
			}
		}
	}

	{
		FOREACH_CONST_Child( &ini2, key )
		{
			FOREACH_CONST_Attr( key, value )
			{
				const std::string &sSection = key->m_sName;
				const std::string &sID = value->first;
				const std::string &sCurrentLanguage = value->second->GetValue<std::string>();
				if( utf8_is_valid(sCurrentLanguage) )
					continue;

				/* The text isn't valid UTF-8.  We don't know what encoding it is; guess that it's
				 * ISO-8859-1 and convert it so it'll display correctly and can be pasted into
				 * the file.  Don't include the original text: if we include multiple encodings
				 * in the resulting text file, editors won't understand the encoding of the
				 * file and will pick one arbitrarily. */
				file.PutLine( ssprintf("Incorrect encoding in section [%s]:", sSection.c_str()) );
				wstring wsConverted = ConvertCodepageToWString( sCurrentLanguage, 1252 );
				std::string sConverted = WStringToRString( wsConverted );
				file.PutLine( ssprintf("%s=%s", sID.c_str(), sConverted.c_str()) );
			}
		}
	}

	{
		FOREACH_CONST_Child( &ini2, key )
		{
			std::vector<std::string> asList;
			const std::string &sSection = key->m_sName;
			FOREACH_CONST_Attr( key, value )
			{
				const std::string &sID = value->first;
				std::string sCurrentLanguage;
				if( ini1.GetValue(sSection, sID, sCurrentLanguage) )
					continue;

				asList.push_back( sID );
			}
			if( !asList.empty() )
			{
				file.PutLine( ssprintf("Obsolete translation in section [%s]:", sSection.c_str()) );
				DumpList( asList, file );
			}
		}
	}

	{
		FOREACH_CONST_Child( &ini1, key )
		{
			std::vector<std::string> asList;
			const std::string &sSection = key->m_sName;
			FOREACH_CONST_Attr( key, value )
			{
				const std::string &sID = value->first;
				const std::string &sBaseText = value->second->GetValue<std::string>();
				if( sBaseText.empty() )
					continue;
				std::string sCurrentLanguage;
				if( ini2.GetValue(sSection, sID, sCurrentLanguage) )
					continue;

				asList.push_back( sID );
			}
			if( !asList.empty() )
			{
				file.PutLine( ssprintf("Not translated in section [%s]:", sSection.c_str()) );
				DumpList( asList, file );
			}
		}
	}

	file.Close();
}
