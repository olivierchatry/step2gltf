#include <iostream>
#include <XCAFApp_Application.hxx>
#include <TDocStd_Document.hxx>
#include <Message_ProgressIndicator.hxx>
#include <Message_ProgressScope.hxx>
#include <iomanip>
#include <mutex>
// STEP Read methods
#include <STEPCAFControl_Reader.hxx>
// STL Write methods
#include <StlAPI_Writer.hxx>
// OBJ Write methods
#include <RWObj_CafWriter.hxx>
// Shape tools
#include <TopoDS_Compound.hxx>
#include <BRep_Builder.hxx>
#include <XCAFPrs_DocumentExplorer.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
// Meshing
#include <TopoDS_Shape.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
// GLTF Write methods
#include <RWGltf_CafWriter.hxx>


/// @name Defines
/// @{
const Standard_Real DefaultLinDeflection = 0.1;
const Standard_Real DefaultAngDeflection = 0.5;
/// @}

/// @name BRepMesh_IncrementalMesh parameters
/// https://www.opencascade.com/doc/occt-7.1.0/overview/html/occt_user_guides__modeling_algos.html#occt_modalg_11_2
/// @{
static Standard_Real g_theLinDeflection = DefaultLinDeflection;
static Standard_Real g_theAngDeflection = DefaultAngDeflection;
/// @}

/// @name Other parameters
/// @{
static int g_verbose_level = 0;
/// @}

/// @name Command line arguments
/// @{
static const char* kHelp                = "-h";
static const char* kHelpLong            = "--help";
static const char* kLinearDeflection    = "--linear";
static const char* kAngularDeflection   = "--angular";
static const char* kVerbose             = "-v";
/// @}

/// @name Error messages
/// @{
static const char* errorInvalidOutExtension = "output filename shall have .glTF, .glb, .stl or .obj extension.";
/// @}

/// Prints progress to stdout
class ProgressIndicator : public Message_ProgressIndicator
{
public:
    virtual void Show(const Message_ProgressScope& /*theScope*/, const Standard_Boolean /*force*/) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const Standard_Real pc = this->GetPosition(); // Always within [0,1]
        
        int percent = static_cast<int>(pc * 100.0);
        if (percent == m_val) return; // Reduce flicker
        m_val = percent;

        // [====================>              ] 50%
        const int width = 40;
        int filled = static_cast<int>(pc * width);
        if (filled > width) filled = width;

        std::cout << "\r[";
        for (int i = 0; i < width; ++i) {
            if (i < filled) std::cout << "=";
            else if (i == filled) std::cout << ">";
            else std::cout << " ";
        }
        std::cout << "] " << std::setw(3) << percent << "% " << std::flush;

        if (percent >= 100) {
            std::cout << std::endl;
        }
    }

    Standard_Boolean UserBreak() override
    {
        return Standard_False;
    }

private:
    int m_val = -1;
    std::mutex m_mutex;
};

/// Transcode STEP to glTF
static int step2stl(char *in, char *out)
{
    enum OutputFormat {
        Format_GLTF,
        Format_GLB,
        Format_STL,
        Format_OBJ
    };
    
    OutputFormat format = Format_GLTF;

    // glTF format depends on output file extension
    const char* out_ext = strrchr(out, '.');
    if (!out_ext) {
        std::cerr << "Error: " << errorInvalidOutExtension << std::endl;
        return 1;
    } else if (strcasecmp(out_ext, ".gltf") == 0) {
        format = Format_GLTF;
    } else if (strcasecmp(out_ext, ".glb") == 0) {
        format = Format_GLB;
    } else if (strcasecmp(out_ext, ".stl") == 0) {
        format = Format_STL;
    } else if (strcasecmp(out_ext, ".obj") == 0) {
        format = Format_OBJ;
    } else {
        std::cerr << "Error: " << errorInvalidOutExtension << std::endl;
        return 1;
    }

    // Creating XCAF document
    Handle(TDocStd_Document) doc;
    Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
    app->NewDocument("MDTV-XCAF", doc);

    Handle(ProgressIndicator) aProgress = new ProgressIndicator();
    Message_ProgressScope aRootScope(aProgress->Start(), "Step2Gltf", 100);

    if (g_verbose_level >= 1) {
        std::cout << "Loading \"" << in << "\" ..." << std::endl;
    }

    // Loading STEP file
    STEPCAFControl_Reader stepReader;
    if (IFSelect_RetDone != stepReader.ReadFile((Standard_CString)in)) {
        std::cerr << "Error: Failed to read STEP file \"" << in << "\" !" << std::endl;
        doc->Close();
        return 1;
    }
    stepReader.SetColorMode(true);
    stepReader.SetNameMode(true);
    stepReader.SetLayerMode(true);

    if (g_verbose_level >= 1) {
        std::cout << "Parsing STEP ..." << std::endl;
    }

    // Transferring to XCAF
    if (!stepReader.Transfer( doc, aRootScope.Next(30) )) {
        std::cerr << "Error: Failed to read STEP file \"" << in << "\" !" << std::endl;
        doc->Close();
        return 1;
    }

    if (g_verbose_level >= 1) {
        std::cout << "Meshing shapes (linear " << g_theLinDeflection
                  << ", angular " << g_theAngDeflection << ") ..." << std::endl;
    }

    XSControl_Reader reader = stepReader.Reader();

    Message_ProgressScope aMeshScope(aRootScope.Next(20), "Meshing", reader.NbShapes());

    for(int shape_id = 1; shape_id <= reader.NbShapes(); shape_id++ )
    {
        if (!aMeshScope.More())
        {
             break;
        }

        TopoDS_Shape shape = reader.Shape( shape_id );

        if (shape.IsNull()) {
            aMeshScope.Next();
            continue;
        }

        BRepMesh_IncrementalMesh Mesh( shape,
            g_theLinDeflection, Standard_False,
            g_theAngDeflection, Standard_True );
        Mesh.Perform(aMeshScope.Next());
    }

    TColStd_IndexedDataMapOfStringString theFileInfo;

    if (g_verbose_level >= 1) {
        std::cout << "Saving to " << out_ext << " ..." << std::endl;
    }

    if (format == Format_GLTF || format == Format_GLB) {
        RWGltf_CafWriter cafWriter(out, format == Format_GLB);
        if (!cafWriter.Perform(doc, theFileInfo, aRootScope.Next(50))) {
             std::cerr << "Error: Failed to write glTF to file !" << std::endl;
             return 1;
        }
    } else if (format == Format_OBJ) {
        RWObj_CafWriter objWriter(out);
        if (!objWriter.Perform(doc, theFileInfo, aRootScope.Next(50))) {
             std::cerr << "Error: Failed to write OBJ to file !" << std::endl;
             return 1;
        }
    } else if (format == Format_STL) {
        // Collect all shapes into a single compound
        TopoDS_Compound aComp;
        BRep_Builder aBuilder;
        aBuilder.MakeCompound(aComp);

        XCAFPrs_DocumentExplorer anExp(doc, XCAFPrs_DocumentExplorerFlags_OnlyLeafNodes);
        for (; anExp.More(); anExp.Next()) {
            const XCAFPrs_DocumentNode& aNode = anExp.Current();
            TopoDS_Shape aShape;
            if (XCAFDoc_ShapeTool::GetShape(aNode.RefLabel, aShape)) {
                aShape.Move(aNode.Location);
                if (!aShape.IsNull()) {
                    aBuilder.Add(aComp, aShape);
                }
            }
        }

        StlAPI_Writer stlWriter;
        stlWriter.Write(aComp, out);
        // StlAPI_Writer is usually fast and synchronous, so we just advance progress
        aRootScope.Next(50);
    }

    return 0;
}


static void show_usage(const char* app)
{
    std::cerr << "stl2gltf - A tool to convert STEP to glTF with OpenCascade" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Usage:" << std::endl;
    std::cerr << "    stl2gltf [OPTIONS] IN_STEP_FILE OUT_GLTF_FILE" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Options:" << std::endl;
    std::cerr << "    " << kLinearDeflection << "=FLOAT      Linear deflection (default: " << DefaultLinDeflection << ")" << std::endl;
    std::cerr << "    " << kAngularDeflection << "=FLOAT     Angular deflection (default: " << DefaultAngDeflection << ")" << std::endl;
    std::cerr << "    -v                  Verbose output" << std::endl;
    std::cerr << "    -h, --help          Display help" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Linear deflection affects final file size." << std::endl;
    std::cerr << std::endl;
    std::cerr << "IN_STEP_FILE is input file in STEP format." << std::endl;
    std::cerr << std::endl;
    std::cerr << "OUT_GLTF_FILE is output file in glTF 2.0 format." << std::endl;
    std::cerr << "File extension defines glTF 2.0 variant:" << std::endl;
    std::cerr << "    \".gltf\" - glTF file and bin file with resourses" << std::endl;
    std::cerr << "    \".glb\"  - binary glTF." << std::endl;
    std::cerr << std::endl;
    std::cerr << "Example usage:" << std::endl;
    std::cerr << "  # Create glTF from STEP file" << std::endl;
    std::cerr << "  step2gltf samples/piggy.step piggy.gltf" << std::endl;
    std::cerr << std::endl;
}


/**
 * @brief Parses command line
 *
 * @return 0 on success, 1 on failure
 */
static Standard_Integer parse_command_line(int argc, char *argv[])
{
    int i;
    char* q;
    for (i = 1; i < argc; i++) {
        char* p = argv[i];
        std::cout << p;

        if (strcmp(p, kHelp) == 0 || strcmp(p, kHelpLong) == 0) {
            show_usage(argv[0]);
            return 1;
        } else if (strcmp(p, kVerbose) == 0) {
            if (g_verbose_level < 1)
                g_verbose_level = 1;
        } else if ( ( q = strchr( p, '=' ) ) != NULL ) {
            *q++ = '\0';
            if( strcmp( p, kLinearDeflection ) == 0 ) {
                g_theLinDeflection = strtod(q, &q);
            } else if ( strcmp( p, kAngularDeflection ) == 0 ) {
                g_theAngDeflection = strtod(q, &q);
            } else {
                std::cerr << "Error: Invalid argument \"" << p << "\"" << std::endl;
                show_usage(argv[0]);
                return 1;
            }
        } else {
            if (i < argc - 2) {
                std::cerr << "Error: Invalid argument \"" << p << "\"" << std::endl;
                show_usage(argv[0]);
                return 1;
            }
            break;
        }
    }

    // check if we have filenames to read and write to.
    if (i != argc - 2) {
        std::cerr << "Error: Missing input and output filenames" << std::endl;
        show_usage(argv[0]);
        return 1;
    }

    return 0;
}


Standard_Integer main (int argc, char *argv[])
{
    // Parses command line
    if (parse_command_line(argc, argv) != 0) {
        return 1;
    }

    return step2stl(argv[argc-2], argv[argc-1]);
}
