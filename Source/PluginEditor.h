/*
  ==============================================================================

    Этот файл содержит базовый фреймворковый код для редактора плагинов JUCE.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

enum FFTOrder
{
    order2048 = 11,
    order4096 = 12,
    order8192 = 13
};

template<typename BlockType>
struct FFTDataGenerator
{
    /**
     генерирует данные БПФ из звукового буфера.
     */
    void produceFFTDataForRendering(const juce::AudioBuffer<float>& audioData, const float negativeInfinity)
    {
        const auto fftSize = getFFTSize();
        
        fftData.assign(fftData.size(), 0);
        auto* readIndex = audioData.getReadPointer(0);
        std::copy(readIndex, readIndex + fftSize, fftData.begin());
        
        // сначала примените оконную функцию к нашим данным
        window->multiplyWithWindowingTable (fftData.data(), fftSize);       // [1]
        
        // затем визуализируем наши данные fft..
        forwardFFT->performFrequencyOnlyForwardTransform (fftData.data());  // [2]
        
        int numBins = (int)fftSize / 2;
        
        
        //нормализуйте значения fft.
        for( int i = 0; i < numBins; ++i )
        {
            auto v = fftData[i];
//            fftData[i] /= (float) numBins;
            if( !std::isinf(v) && !std::isnan(v) )
            {
                v /= float(numBins);
            }
            else
            {
                v = 0.f;
            }
            fftData[i] = v;
            
        }
        
        //преобразуйте их в децибелы
        for( int i = 0; i < numBins; ++i )
        {
            fftData[i] = juce::Decibels::gainToDecibels(fftData[i], negativeInfinity);
        }
        
        fftDataFifo.push(fftData);
    }
    
    void changeOrder(FFTOrder newOrder)
    {
        //когда вы меняете порядок, заново открываете окно, пересылаете FFT, fifo, fftData
        //также сбросьте fifoIndex
        //вещи, которые нуждаются в воссоздании, должны быть созданы в куче с помощью std::make_unique<>
        
        order = newOrder;
        auto fftSize = getFFTSize();
        
        forwardFFT = std::make_unique<juce::dsp::FFT>(order);
        window = std::make_unique<juce::dsp::WindowingFunction<float>>(fftSize, juce::dsp::WindowingFunction<float>::blackmanHarris);
        
        fftData.clear();
        fftData.resize(fftSize * 2, 0);

        fftDataFifo.prepare(fftData.size());
    }
    //==============================================================================
    int getFFTSize() const { return 1 << order; }
    int getNumAvailableFFTDataBlocks() const { return fftDataFifo.getNumAvailableForReading(); }
    //==============================================================================
    bool getFFTData(BlockType& fftData) { return fftDataFifo.pull(fftData); }
private:
    FFTOrder order;
    BlockType fftData;
    std::unique_ptr<juce::dsp::FFT> forwardFFT;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> window;
    
    Fifo<BlockType> fftDataFifo;
};

struct FFTSample
{
    FFTSample(std::vector<float> retorDtata) {
        this->retorDtata = retorDtata;
    }

    float getY(int iter) { return retorDtata[iter]; }
    std::vector<float> getData() { return retorDtata; }
private:
    std::vector<float> retorDtata;
};

template<typename PathType>
struct AnalyzerPathGenerator
{
    /*
     converts 'renderData[]' into a juce::Path
     */
    void generatePath(const std::vector<float>& renderData,
                      juce::Rectangle<float> fftBounds,
                      int fftSize,
                      float binWidth,
                      float negativeInfinity)
    {
        auto top = fftBounds.getY();
        auto bottom = fftBounds.getHeight();
        auto width = fftBounds.getWidth();

        int numBins = (int)fftSize / 2;

        PathType p;
        p.preallocateSpace(3 * (int)fftBounds.getWidth());

        auto map = [bottom, top, negativeInfinity](float v)
        {
            return juce::jmap(v,
                              negativeInfinity, 0.f,
                              float(bottom+10),   top);
        };

        auto y = map(renderData[0]);

//        jassert( !std::isnan(y) && !std::isinf(y) );
        if( std::isnan(y) || std::isinf(y) )
            y = bottom;
        
        p.startNewSubPath(0, y);

        const int pathResolution = 1; //вы можете провести линию к каждому пикселю "Разрешения пути".
        

        for( int binNum = 1; binNum < numBins; binNum += pathResolution )
        {
            y = map(renderData[binNum]);

//            jassert( !std::isnan(y) && !std::isinf(y) );

            if( !std::isnan(y) && !std::isinf(y) )
            {
                auto binFreq = binNum * binWidth;
                auto normalizedBinX = juce::mapFromLog10(binFreq, 20.f, 20000.f);
                int binX = std::floor(normalizedBinX * width);
                p.lineTo(binX, y);
            }
        }

        //тут можно подрезать финальные значения ачх

        pathFifo.push(p);
    }

    FFTSample analize(const std::vector<float>& renderData,
        int fftSize,
        float binWidth,
        float negativeInfinity)
    {
        auto top = 1000.f;
        auto bottom = 0.f;

        int numBins = (int)fftSize / 2;


        auto map = [bottom, top, negativeInfinity](float v)
        {
            return juce::jmap(v,
                negativeInfinity, 0.f,
                bottom, top);
        };

        auto y = map(renderData[0]);

        //        jassert( !std::isnan(y) && !std::isinf(y) );
        if (std::isnan(y) || std::isinf(y))
            y = bottom;

        std::vector<float> returnSamle;

        for (int binNum = 1; binNum < numBins; binNum += 1)
        {
            y = map(renderData[binNum]);
            returnSamle.push_back(y);
        }

        return FFTSample(returnSamle);
    }

    int getNumPathsAvailable() const
    {
        return pathFifo.getNumAvailableForReading();
    }

    bool getPath(PathType& path)
    {
        return pathFifo.pull(path);
    }


private:
    Fifo<PathType> pathFifo;
};

struct LookAndFeel : juce::LookAndFeel_V4
{
    void drawRotarySlider (juce::Graphics&,
                           int x, int y, int width, int height,
                           float sliderPosProportional,
                           float rotaryStartAngle,
                           float rotaryEndAngle,
                           juce::Slider&) override;
    
    void drawToggleButton (juce::Graphics &g,
                           juce::ToggleButton & toggleButton,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;
};

struct RotarySliderWithLabels : juce::Slider
{
    RotarySliderWithLabels(juce::RangedAudioParameter& rap, const juce::String& unitSuffix) :
    juce::Slider(juce::Slider::SliderStyle::RotaryHorizontalVerticalDrag,
                 juce::Slider::TextEntryBoxPosition::NoTextBox),
    param(&rap),
    suffix(unitSuffix)
    {
        setLookAndFeel(&lnf);
    }
    
    ~RotarySliderWithLabels()
    {
        setLookAndFeel(nullptr);
    }
    
    struct LabelPos
    {
        float pos;
        juce::String label;
    };
    
    juce::Array<LabelPos> labels;
    
    void paint(juce::Graphics& g) override;
    juce::Rectangle<int> getSliderBounds() const;
    int getTextHeight() const { return 14; }
    juce::String getDisplayString() const;
private:
    LookAndFeel lnf;
    
    juce::RangedAudioParameter* param;
    juce::String suffix;
};



struct PathProducer
{
    PathProducer(SingleChannelSampleFifo<SimpleEQAudioProcessor::BlockType>& scsf) :
    leftChannelFifo(&scsf)
    {
        leftChannelFFTDataGenerator.changeOrder(FFTOrder::order2048);
        monoBuffer.setSize(1, leftChannelFFTDataGenerator.getFFTSize());
    }
    void process(juce::Rectangle<float> fftBounds, double sampleRate, bool autoON);
    juce::Path getPath() { return leftChannelFFTPath; }
    std::list<FFTSample> getFFTSample() { return retorDtata; }
    void pushFFTSamle(FFTSample sample) { 
        retorDtata.push_back(sample);
        if (retorDtata.size() > 4000)
        {
            retorDtata.pop_front();
        }
         
        
    }
    
    void generateNewFilters() {
        std::vector<float> summData = retorDtata.begin()->getData();
        int size = retorDtata.size();
        retorDtata.pop_front();

        for (auto i = retorDtata.begin(); i != retorDtata.end(); i++)
        {
            for (int j = 0; j < summData.size(); j++)
            {
                summData[j] += i->getY(j);
            }
        }
        for (int j = 0; j < summData.size(); j++)
        {
            summData[j] = summData[j]/size;
        }
        // получили среднее значение для каждой полученной чистоты
        retorDtata.clear();

        int lowpick = 0;
        int higtpick = size - 1;
        float lowMax = summData[0];
        for (int i = 1; lowMax <= summData[i]; i++)
        {
            lowMax = summData[i];
        }



    }


private:
    SingleChannelSampleFifo<SimpleEQAudioProcessor::BlockType>* leftChannelFifo;
    
    juce::AudioBuffer<float> monoBuffer;

    std::list<FFTSample> retorDtata;
    
    FFTDataGenerator<std::vector<float>> leftChannelFFTDataGenerator;
    
    AnalyzerPathGenerator<juce::Path> pathProducer;

    bool itWasAnalized = false;
    
    juce::Path leftChannelFFTPath;
};

struct ResponseCurveComponent: juce::Component,
juce::AudioProcessorParameter::Listener,
juce::Timer
{
    ResponseCurveComponent(SimpleEQAudioProcessor&);
    ~ResponseCurveComponent();
    
    void parameterValueChanged (int parameterIndex, float newValue) override;

    void parameterGestureChanged (int parameterIndex, bool gestureIsStarting) override { }
    
    void timerCallback() override;
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    
    void toggleAnalysisEnablement(bool enabled)
    {
        shouldShowFFTAnalysis = enabled;
    }

    void toggleAutoEnablement(bool enabled) {
        recordPicsEnable = enabled;
    }
private:
    SimpleEQAudioProcessor& audioProcessor;

    bool shouldShowFFTAnalysis = true;

    bool recordPicsEnable = false;

    juce::Atomic<bool> parametersChanged { false };
    
    MonoChain monoChain;

    void updateResponseCurve();
    
    juce::Path responseCurve;

    void updateChain();
    
    void drawBackgroundGrid(juce::Graphics& g);
    void drawTextLabels(juce::Graphics& g);
    
    std::vector<float> getFrequencies();
    std::vector<float> getGains();
    std::vector<float> getXs(const std::vector<float>& freqs, float left, float width);

    juce::Rectangle<int> getRenderArea();
    
    juce::Rectangle<int> getAnalysisArea();
    
    PathProducer leftPathProducer, rightPathProducer;
};
//==============================================================================
struct PowerButton : juce::ToggleButton { };

struct AnalyzerButton : juce::ToggleButton
{
    void resized() override
    {
        auto bounds = getLocalBounds();
        auto insetRect = bounds.reduced(4);
        
        randomPath.clear();
        
        juce::Random r;
        
        randomPath.startNewSubPath(insetRect.getX(),
                                   insetRect.getY() + insetRect.getHeight() * r.nextFloat());
        
        for( auto x = insetRect.getX() + 1; x < insetRect.getRight(); x += 2 )
        {
            randomPath.lineTo(x,
                              insetRect.getY() + insetRect.getHeight() * r.nextFloat());
        }
    }
    
    juce::Path randomPath;
};
/**
*/
class SimpleEQAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    SimpleEQAudioProcessorEditor (SimpleEQAudioProcessor&);
    ~SimpleEQAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // Эта ссылка предназначена для того, чтобы ваш редактор мог 
    // быстро получить доступ к объекту processor, который его создал.
    SimpleEQAudioProcessor& audioProcessor;

    
    RotarySliderWithLabels peakFreqSlider,
    peakGainSlider,
    peakQualitySlider,
    lowCutFreqSlider,
    highCutFreqSlider,
    lowCutSlopeSlider,
    highCutSlopeSlider;
    
    ResponseCurveComponent responseCurveComponent;
    
    using APVTS = juce::AudioProcessorValueTreeState;
    using Attachment = APVTS::SliderAttachment;
    
    Attachment peakFreqSliderAttachment,
                peakGainSliderAttachment,
                peakQualitySliderAttachment,
                lowCutFreqSliderAttachment,
                highCutFreqSliderAttachment,
                lowCutSlopeSliderAttachment,
                highCutSlopeSliderAttachment;

    std::vector<juce::Component*> getComps();
    
    PowerButton lowcutBypassButton, peakBypassButton, highcutBypassButton, autoEnabledButton;
    AnalyzerButton analyzerEnabledButton;

    
    using ButtonAttachment = APVTS::ButtonAttachment;
    
    ButtonAttachment    lowcutBypassButtonAttachment,
                        peakBypassButtonAttachment,
                        highcutBypassButtonAttachment,
                        analyzerEnabledButtonAttachment,
                        autoEnabledButtonAttachment;
    
    LookAndFeel lnf;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleEQAudioProcessorEditor)
};
